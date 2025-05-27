//#define HOSPITAL 1
#ifdef HOSPITAL
    //HOSPITAL 
    const char* ssid = "WPROFESIONALES";
    const char* pass = "temporal%";
    #define _URL "https://hospital.almansa.ovh/api/"
#else
    /* CASA */
    const char* ssid = "Mascamierdas";
    const char* pass = "Gominolas69";
    #define _URL "http://192.168.1.248/mensajeria/api/"
#endif