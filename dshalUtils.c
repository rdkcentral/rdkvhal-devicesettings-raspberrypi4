

#include <stdio.h>
#include <ctype.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>

#include "dshalUtils.h"

static uint16_t initialised = 0;
VCHI_INSTANCE_T    vchi_instance;
VCHI_CONNECTION_T *vchi_connection;

int vchi_tv_init()
{
    int res = 0;
    if (!initialised)
    {
        vcos_init();
        res = vchi_initialise( &vchi_instance );
        if ( res != 0 )
        {
          printf( "Failed to initialize VCHI (res=%d)", res );
          return res;
        }

        res = vchi_connect( NULL, 0, vchi_instance );
        if ( res != 0)
        {
          printf( "Failed to create VCHI connection (ret=%d)", res );
          return res;
        }

        // Initialize the tvservice
        vc_vchi_tv_init( vchi_instance, &vchi_connection, 1 );
        // Initialize the gencmd
        vc_vchi_gencmd_init(vchi_instance, &vchi_connection, 1 );
        initialised = 1;
    }
    return res;
}

int vchi_tv_uninit()
{
    int res = 0;
    if (initialised)
    {
        // Stop the tvservice
        vc_vchi_tv_stop();
        vc_gencmd_stop();
        // Disconnect the VCHI connection
        vchi_disconnect( vchi_instance );
        initialised = 0;
    }
    return res;
}

static int detailedBlock(unsigned char *x, int extension, dsDisplayEDID_t *displayEdidInfo)
{
    static unsigned char name[53];
    switch (x[3]) {
        case 0xFC:
                if (strchr((char *)name, '\n')) return 1;
                strncat((char *)name, (char *)x + 5, 13);
                strncpy(displayEdidInfo->monitorName , (const char *)name, dsEEDID_MAX_MON_NAME_LENGTH);
                return 1;
           default:
               return 1;
    }
}

static void hdmi_cea_block(unsigned char *x, dsDisplayEDID_t *displayEdidInfo)
{
    displayEdidInfo->physicalAddressA = (x[4] >> 4);
    displayEdidInfo->physicalAddressB = (x[4] & 0x0f);
    displayEdidInfo->physicalAddressC = (x[5] >> 4);
    displayEdidInfo->physicalAddressD = (x[5] & 0x0f);
   if (displayEdidInfo->physicalAddressB)
       displayEdidInfo->isRepeater = true;
   else
       displayEdidInfo->isRepeater = false;
}

static void cea_block(unsigned char *x, dsDisplayEDID_t *displayEdidInfo)
{
    unsigned int oui;
    switch ((x[0] & 0xe0) >> 5) {
        case 0x03:
            oui = (x[3] << 16) + (x[2] << 8) + x[1];
            if (oui == 0x000c03) {
                hdmi_cea_block(x, displayEdidInfo);
                displayEdidInfo->hdmiDeviceType = true;
            }
            break;
        default:
            break;
    }
}

static int parse_cea_block(unsigned char *x, dsDisplayEDID_t *displayEdidInfo)
{
    int ret = 0;
    int version = x[1];
    int offset = x[2];
    unsigned char *detailed_buf;
     if (version == 3) {
            for (int i = 4; i < offset; i += (x[i] & 0x1f) + 1) {
                cea_block(x + i, displayEdidInfo);
            }
    }
    for (detailed_buf = x + offset; detailed_buf + 18 < x + 127; detailed_buf += 18)
            if (detailed_buf[0])
                detailedBlock(detailed_buf, 1, displayEdidInfo);
    return ret;
}

void fill_edid_struct(unsigned char *edidBytes, dsDisplayEDID_t *displayEdidInfo, int size)
{
    unsigned char *x;
    time_t t;
    struct tm *localtm;
    int i;
    if (!edidBytes || memcmp(edidBytes, "\x00\xFF\xFF\xFF\xFF\xFF\xFF\x00", 8)) {
        printf("header Not found\n");
        return;
    }
    displayEdidInfo->productCode = edidBytes[0x0A] + (edidBytes[0x0B] << 8);
    displayEdidInfo->serialNumber = (edidBytes[0x0C] + (edidBytes[0x0D] << 8) + (edidBytes[0x0E] << 16)
                                      + (edidBytes[0x0F] << 24));
    displayEdidInfo->hdmiDeviceType = true;  // This is true for Rpi
    time(&t);
    localtm = localtime(&t);
    if (edidBytes[0x10] < 55 || edidBytes[0x10] == 0xff) {
        if (edidBytes[0x11] > 0x0f) {
            if (edidBytes[0x10] == 0xff) {
                displayEdidInfo->manufactureWeek = edidBytes[0x10];
                displayEdidInfo->manufactureYear = edidBytes[0x11];
            } else if (edidBytes[0x11] + 90 <= localtm->tm_year) {
                displayEdidInfo->manufactureWeek = edidBytes[0x10];
                displayEdidInfo->manufactureYear = edidBytes[0x11] + 1990;
            }
        }
    }
    detailedBlock(edidBytes + 0x36, 0, displayEdidInfo);
    detailedBlock(edidBytes + 0x48, 0, displayEdidInfo);
    detailedBlock(edidBytes + 0x5A, 0, displayEdidInfo);
    detailedBlock(edidBytes + 0x6C, 0, displayEdidInfo);
    x = edidBytes;
    for (i = 128; i < size; i += 128) {
        if (x[0] == 0x02)
           parse_cea_block(x, displayEdidInfo);
    }
}

/* Log scheme for DSHAL */
static int logflag = 0;

const char* log_type_to_string(int type)
{
    switch (type) {
        case DSHAL_LOG_ERROR: return "DSHAL-ERROR";
        case DSHAL_LOG_WARNING: return "DSHAL-WARNING";
        case DSHAL_LOG_INFO: return "DSHAL-INFO";
        case DSHAL_LOG_DEBUG: return "DSHAL-DEBUG";
        default: return "DSHAL-DEFAULT";
    }
}

void logger(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vfprintf(stdout, format, args);
    fprintf(stdout, "\n");
    va_end(args);
}

int getLogFlag(void)
{
    return logflag;
}

void configDSHALLogging(void)
{
    if (access(LOG_CONFIG_FILE, F_OK) == -1) {
        perror("DSHAL configDSHALLogging error accessing debug.ini\n");
        return;
    }
    FILE *file = fopen(LOG_CONFIG_FILE, "r");
    if (!file) {
        perror("DSHAL configDSHALLogging error fopen debug.ini\n");
        return;
    }

    char line[512] = {0};
    const struct {
        const char *nameEnabled;
        const char *nameDisabled;
        int flag;
    } log_levels[] = {
        {"ERROR", "!ERROR", LOG_ERROR},
        {"WARNING", "!WARNING", LOG_WARNING},
        {"INFO", "!INFO", LOG_INFO},
        {"DEBUG", "!DEBUG", LOG_DEBUG}
    };

    while (fgets(line, sizeof(line), file)) {
        line[strcspn(line, "\n")] = '\0';
        // Check for the LOG.RDK.DSMGR entry
        if (strncmp(line, "LOG.RDK.DSMGR", 13) == 0) {
            for (size_t i = 0; i < sizeof(log_levels) / sizeof(log_levels[0]); ++i) {
                if (strstr(line, log_levels[i].nameEnabled)) {
                    logflag |= log_levels[i].flag;
                } else if (strstr(line, log_levels[i].nameDisabled)) {
                    logflag &= ~log_levels[i].flag;
                }
            }
            break;
        }
    }
    printf("DSHAL configDSHALLogging logflag: 0x%x\n", logflag);
    fclose(file);
}

// Make constructor complete the logging config when it's loaded into memory.
static void __attribute__((constructor)) initDSHALLogging(void)
{
    configDSHALLogging();
}
