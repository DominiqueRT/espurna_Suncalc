/*
SUNCALC MODULE

Copyright (C) 2018 by 

calculation based on : https://www.aa.quae.nl/en/reken/zonpositie.html
*/

#if SUNCALC_SUPPORT

    #include <TimeLib.h>
    #include <NtpClientLib.h>

    #define EXTRA_DEBUG_SUPPORT 1

    bool _update_sun_calculation = true;
    static unsigned char _day_of_last_calculation = 0;

    long sunrise=0, solarNoon=0, sunset=0;

    #if WEB_SUPPORT
        bool _suncalcWebSocketOnReceive(const char * key, JsonVariant& value) {
            return (strncmp(key, "suncalc", 7) == 0);
        }

        void _suncalcWebSocketOnSend(JsonObject& root) {
            root["suncalcVisible"] = 1;
            root["suncalcLatitude"] = atof(getSetting("suncalcLatitude", SUNCALC_LATITUDE).c_str());
            root["suncalcLongitude"] = atof(getSetting("suncalcLongitude", SUNCALC_LONGITUDE).c_str());
            root["suncalcDawnDuskType"] = getSetting("suncalcDawnDuskType", SUNCALC_DAWN_DUSK_TYPE).toInt();

            if (year()>2017)
            {
                updateSunCalc();

                root["sunrise"] = sunrise;
                root["solarNoon"] = solarNoon;
                root["sunset"] = sunset;   
            }    
        }
    #endif

    #define J2000			2451545.0 // J2000 is the Julian date corresponding to January 1.5, year 2000.
    #define JulianCentury	36525.0 // JulianCentury in days.
    #define Er				6378.14 //  Earth radius in Km.
    #define Fl				1/298.257 // IAU 1976 values
    #define J1970			2440588
    #define sInDay			86400.0

    void _MsgDbl(double doubleValue, string msg)
    {
        #if EXTRA_DEBUG_SUPPORT
            char fValue[8];
            dtostrf(doubleValue, 8, 6, fValue);
            DEBUG_MSG_P(PSTR("[Suncalc] %s : %s\n"),(char *) msg.c_str(), fValue);    
        #endif
    }

    double findMod(double a, double b) // specific modulo calculation as linking with findMod fail
    {
        // Handling negative values 
        if (a < 0)
            a = -a;
        if (b < 0)
            b = -b;

        // Finding mod by repeated subtraction 
        double mod = a;
        while (mod >= b)
            mod = mod - b;

        // Sign of result typically depends 
        // on sign of a. 
        if (a < 0)
            return -mod;

        return mod;
    };
    
    string formatNum(int num) {
        char buf[2];
        sprintf(buf,"%i",num);
	    return buf;
    }

    string secondsToHMSStr(long secs)
    {
        if (secs <= 0)
            return "none";

        int seconds = secs % 60;
        secs /= 60;
        int minutes = secs % 60;
        secs /= 60;
        int hours = secs % 24;

        return (hours < 10 ? "0" : "") + formatNum(hours) + ":" + (minutes < 10 ? "0" : "") + formatNum(minutes) + ":" + (seconds < 10 ? "0" : "") + formatNum(seconds);
    }
  
    double dateToJulianDay(time_t date, double tz) {
        return floor(date / 24.0 / 3600. + tz / 24 + J1970);
    }

    double jdToJulianCentury(double jd) {
        return (jd - J2000) / JulianCentury;
    }

    double jCenturyToGeomMeanLongSun(double jCentury) {
        return findMod(280.46646 + jCentury * (36000.76983 + jCentury * 0.0003032), 360.0);
    };

    double jCenturyToGeomMeanAnomSun(double jCentury) {
        return  357.52911 + jCentury * (35999.05029 - 0.0001537*jCentury);
    }

    double jCenturyToEccentEarthOrbit(double jCentury) {
        return 0.016708634 - jCentury * (0.000042037 + 0.0000001267*jCentury);
    }

    double SunEqofCtr(double jCentury, double geomMeanAnom)
    {
        return sin(geomMeanAnom * PI / 180.0)*(1.914602 - jCentury * (0.004817 + 0.000014*jCentury))
            + sin(2 * geomMeanAnom*PI / 180.0)*(0.019993 - 0.000101*jCentury)
            + sin(3 * geomMeanAnom*PI / 180.0)*0.000289;
    }

    double SunTrueLong(double geomMeanLong, double SunEqofCtr) {
        return geomMeanLong + SunEqofCtr;
    }
    double SunTrueAnom(double geomMeanAnom, double sunEqOfCenter) {
        return geomMeanAnom + sunEqOfCenter;
    }

    double RADIANS(double deg) {
        return deg * PI / 180.0;
    }

    double SunRadVector(double eccentEarthOrbit, double sunTrueAnom) {
        return (1.000001018*(1 - eccentEarthOrbit * eccentEarthOrbit)) / (1 + eccentEarthOrbit * cos(RADIANS(sunTrueAnom)));

    }

    double SunAppLong(double sunTrueLong, double jCentury) {
        return sunTrueLong - 0.00569 - 0.00478*sin(RADIANS(125.04 - 1934.136*jCentury));
    }

    double MeanObliqEcliptic(double jCentury) {
        return  23 + (26 + ((21.448 - jCentury * (46.815 + jCentury * (0.00059 - jCentury * 0.001813)))) / 60) / 60;
    }

    double ObliqCorr(double meanObliqEcliptic, double jCentury) {
        return  meanObliqEcliptic + 0.00256*cos(RADIANS(125.04 - 1934.136*jCentury));
    }

    double Degres(double rad) {
        return rad * 180 / PI;
    }

    double SunRtAscen(double sunAppLong, double obliqCorr) {
        return Degres(atan2(cos(RADIANS(obliqCorr))*sin(RADIANS(sunAppLong)), cos(RADIANS(sunAppLong))));
    }

    double SunDeclin(double sunAppLong, double obliqCorr) {
        return Degres(asin(sin(RADIANS(obliqCorr))*sin(RADIANS(sunAppLong))));
    }

    double y(double obliqCorr) {
        return tan(RADIANS(obliqCorr / 2))*tan(RADIANS(obliqCorr / 2));
    }

    double EqofTime(double y, double geomMeanLongSun, double geomMeanAnom, double eccentEarthOrbit) {
        return 4 * Degres(y*sin(2 * RADIANS(geomMeanLongSun)) - 2 * eccentEarthOrbit*sin(RADIANS(geomMeanAnom)) + 4 * eccentEarthOrbit*y*sin(RADIANS(geomMeanAnom))
            *cos(2 * RADIANS(geomMeanLongSun)) - 0.5*y*y*sin(4 * RADIANS(geomMeanLongSun)) - 1.25*eccentEarthOrbit*eccentEarthOrbit*sin(2 * RADIANS(geomMeanAnom)));
    }

    double HASunrise(double Lat, double sunDeclin, int  twilight) {
        double twilightAngle = 0;
        if (twilight==0)
            twilightAngle = 0.833;
        else
            twilightAngle = 6 * twilight;

        twilightAngle += 90;
        return Degres(acos(cos(RADIANS(twilightAngle)) / (cos(RADIANS(Lat))*cos(RADIANS(sunDeclin)))
            - tan(RADIANS(Lat))*tan(RADIANS(sunDeclin))));
    }

    double SolarNoon(double Long, double EqOfTime, double TZ) {
        return (720 - 4 * Long - EqOfTime + TZ * 60) *60 ;
    }

    double SunriseTime(double solarNoon, double haSunrise) {
        return  solarNoon  - haSunrise * 4*60;
    }

    double SunsetTime(double solarNoon, double haSunrise) {
        return solarNoon  + haSunrise * 4 * 60;
    }

void updateSunCalc(){
    if(_update_sun_calculation)
    {
        time_t actualTime = now();

        DEBUG_MSG_P(PSTR("[Suncalc] update time : %s\n"), (char *)ntpDateTime(actualTime).c_str());
        
        double tz = getSetting("ntpOffset", NTP_TIME_OFFSET).toInt() / 60.0;
        
        _MsgDbl(tz, "Time Zone");

        double latitude = atof(getSetting("suncalcLatitude", SUNCALC_LATITUDE).c_str());
        double longitude = atof(getSetting("suncalcLongitude", SUNCALC_LONGITUDE).c_str());
        int dawnDusk =  getSetting("suncalcDawnDuskType", SUNCALC_DAWN_DUSK_TYPE).toInt();
    
        _MsgDbl(latitude,"Latitude");
        _MsgDbl(longitude,"Longitude");

        double jd = dateToJulianDay(actualTime, tz);

        double jCentury = jdToJulianCentury(jd);
        double geomMeanLongSun = jCenturyToGeomMeanLongSun(jCentury);
        double geomMeanAnomSun = jCenturyToGeomMeanAnomSun(jCentury);
        double eccentEarthOrbit = jCenturyToEccentEarthOrbit(jCentury);
        double sunEqofCtr = SunEqofCtr(jCentury, geomMeanAnomSun);
        double sunTrueLong = SunTrueLong(geomMeanLongSun, sunEqofCtr);
        double sunTrueAnom = SunTrueAnom(geomMeanAnomSun, sunEqofCtr);
        double sunRadVector = SunRadVector(eccentEarthOrbit, sunTrueAnom);
        double sunAppLong = SunAppLong(sunTrueLong, jCentury);

        double meanObliqEcliptic = MeanObliqEcliptic(jCentury);
        double obliqCorr = ObliqCorr(meanObliqEcliptic, jCentury);
        double sunRtAscen = SunRtAscen(sunAppLong, obliqCorr);
        double sunDeclin = SunDeclin(sunAppLong, obliqCorr);
        double _y = y(obliqCorr);
        double eqOfTime = EqofTime(_y, geomMeanLongSun, geomMeanAnomSun, eccentEarthOrbit);
        double haSunrise = HASunrise(latitude, sunDeclin, dawnDusk);
        
        solarNoon = SolarNoon(longitude, eqOfTime, tz) ;
        sunrise = SunriseTime(solarNoon, haSunrise) ;
        sunset = SunsetTime(solarNoon, haSunrise) ;
            
        DEBUG_MSG_P(PSTR("[suncalc] sunrise : %s\n"), (char *)  secondsToHMSStr(sunrise).c_str());
        DEBUG_MSG_P(PSTR("[suncalc] transit : %s\n"), (char *) secondsToHMSStr(solarNoon).c_str());
        DEBUG_MSG_P(PSTR("[suncalc] sunset : %s\n"), (char *) secondsToHMSStr(sunset).c_str());

        _update_sun_calculation=false;

        #if WEB_SUPPORT
            wsSend(_suncalcWebSocketOnSend);
        #endif
        
    }
}
    
void _suncalcConfigure() {
     if (year()>2017)
    {
        updateSunCalc();
    }
}

void _suncalcLoop() {
    unsigned char actualDay = day();

    if (year()>2017)
    {      
        if (actualDay != _day_of_last_calculation )
            {
                DEBUG_MSG_P(PSTR("[Suncalc] Actual Day: %i\n"), actualDay);
                DEBUG_MSG_P(PSTR("[Suncalc] Day Of Calculation: %i\n"), _day_of_last_calculation);
       
                _update_sun_calculation= true;
                _day_of_last_calculation=actualDay;
       
                updateSunCalc();
            }
    }    
}

void suncalcSetup() {
    _day_of_last_calculation=0;

    #if WEB_SUPPORT
        wsOnSendRegister(_suncalcWebSocketOnSend);
        wsOnReceiveRegister(_suncalcWebSocketOnReceive);
    #endif  // WEB_SUPPORT

    // Main callbacks
    espurnaRegisterLoop(_suncalcLoop);
    espurnaRegisterReload([]() { _update_sun_calculation = true; });
}

#endif // suncalc_SUPPORT
