diff --git a/dsVideoPort.c b/dsVideoPort.c
index 174626b..7d3e9b9 100644
--- a/dsVideoPort.c
+++ b/dsVideoPort.c
@@ -673,25 +673,7 @@ dsError_t dsGetResolution(intptr_t handle, dsVideoPortResolution_t *resolution)
 					*end = '\0';
 					strncpy(wstresolution, start, sizeof(wstresolution));
 					hal_info("Resolution string: '%s'\n", wstresolution);
-					int Width = 0;
-					int Height = 0;
-					int FrameRate = 0;
-					resolution->interlaced = ((strstr(wstresolution, "i") != NULL) ? true : false);
-
-					if (sscanf(wstresolution, "%dx%dpx%d", &Width, &Height, &FrameRate) == 3 ||
-						sscanf(wstresolution, "%dx%dix%d", &Width, &Height, &FrameRate) == 3) {
-						resolution->pixelResolution = getdsVideoResolution(Width, Height);
-						resolution->aspectRatio = getAspectRatioFromWidthHeight(Width, Height);
-						resolution->frameRate = getdsVideoFrameRate(FrameRate);
-						snprintf(resolution->name, sizeof(resolution->name), "%d%c%d", Height, (resolution->interlaced ? 'i' : 'p'), FrameRate);
-						hal_dbg(
-							"Resolution: '%s', pixelResolution: '%u', aspectRatio: '%u', frameRate: '%u'\n",
-							resolution->name, resolution->pixelResolution, resolution->aspectRatio, resolution->frameRate);
-						return dsERR_NONE;
-					} else {
-						hal_err("Failed to parse resolution string\n");
-						return dsERR_GENERAL;
-					}
+					return (convertWesterosResolutionTokResolution(wstresolution, resolution)? dsERR_NONE: dsERR_GENERAL);
 				} else {
 					hal_err("Failed to parse westerosRWWrapper response; ']' not found.\n");
 					return dsERR_GENERAL;
@@ -794,7 +776,6 @@ static uint32_t dsGetHdmiMode(dsVideoPortResolution_t *resolution)
  */
 dsError_t dsSetResolution(intptr_t handle, dsVideoPortResolution_t *resolution)
 {
-    /* Auto Select uses 720p. Should be converted to dsVideoPortResolution_t = 720p in DS-VOPConfig, not here */
     hal_info("invoked.\n");
     VOPHandle_t *vopHandle = (VOPHandle_t *)handle;
     if (false == _bIsVideoPortInitialized) {
@@ -805,11 +786,26 @@ dsError_t dsSetResolution(intptr_t handle, dsVideoPortResolution_t *resolution)
         return dsERR_INVALID_PARAM;
     }
     if (vopHandle->m_vType == dsVIDEOPORT_TYPE_HDMI) {
-        hal_dbg("Setting HDMI resolution '%s'\n", resolution->name);
-        uint32_t hdmi_mode = dsGetHdmiMode(resolution);
-		const char *westerosRes = getWesterosResolutionFromVic(hdmi_mode);
+        hal_dbg("Setting HDMI resolution name'%s'\n", resolution->name);
+		hal_dbg("Setting HDMI resolution pixelResolution '0x%x'\n", resolution->pixelResolution);
+		hal_dbg("Setting HDMI resolution aspectRatio '0x%x'\n", resolution->aspectRatio);
+		hal_dbg("Setting HDMI resolution frameRate '%s'\n", getdsVideoFrameRateString(resolution->frameRate));
+		hal_dbg("Setting HDMI resolution interlaced '%d'\n", resolution->interlaced);
+		if (!dsVideoPortFrameRate_isValid(resolution->frameRate) ||
+			!dsVideoPortPixelResolution_isValid(resolution->pixelResolution)) {
+			hal_err("Invalid %s: '%s'\n",
+					!dsVideoPortFrameRate_isValid(resolution->frameRate) ? "frameRate" : "pixelResolution",
+					!dsVideoPortFrameRate_isValid(resolution->frameRate) ?
+						getdsVideoFrameRateString(resolution->frameRate) :
+						getdsVideoResolutionString(resolution->pixelResolution));
+			return dsERR_INVALID_PARAM;
+		}
+        // ToDO: implement it.
+		// get westeros resolution string for the given resolution and frame rate
 		char cmd[256] = {0};
 		char data[128] = {0};
+		char westerosRes[64] = {0};
+
 		if (westerosRes == NULL) {
 			hal_err("Failed to convert resolution '%s' to westeros format\n", resolution->name);
 			return dsERR_GENERAL;
@@ -1100,75 +1096,12 @@ dsError_t dsSupportedTvResolutions(intptr_t handle, int *resolutions)
         for (i = 0; i < num_of_modes; i++) {
             hal_info("[%d] mode %u: %ux%u%s%uHz\n", i, modeSupported[i].code, modeSupported[i].width,
                     modeSupported[i].height, (modeSupported[i].scan_mode?"i":"p"), modeSupported[i].frame_rate);
-#if 0
-            switch (modeSupported[i].code) {
-                case HDMI_CEA_480p60:
-                case HDMI_CEA_480p60H:
-                case HDMI_CEA_480p60_2x:
-                case HDMI_CEA_480p60_2xH:
-                case HDMI_CEA_480p60_4x:
-                case HDMI_CEA_480p60_4xH:
-                    *resolutions |= dsTV_RESOLUTION_480p;
-                    break;
-                case HDMI_CEA_480i60:
-                case HDMI_CEA_480i60H:
-                case HDMI_CEA_480i60_4x:
-                case HDMI_CEA_480i60_4xH:
-                    *resolutions |= dsTV_RESOLUTION_480i;
-                    break;
-                case HDMI_CEA_576i50:
-                case HDMI_CEA_576i50H:
-                case HDMI_CEA_576i50_4x:
-                case HDMI_CEA_576i50_4xH:
-                    *resolutions |= dsTV_RESOLUTION_576i;
-                    break;
-                case HDMI_CEA_576p50:
-                case HDMI_CEA_576p50H:
-                case HDMI_CEA_576p50_2x:
-                case HDMI_CEA_576p50_2xH:
-                case HDMI_CEA_576p50_4x:
-                case HDMI_CEA_576p50_4xH:
-                    *resolutions |= dsTV_RESOLUTION_576p50;
-                    break;
-                case HDMI_CEA_720p50:
-                    *resolutions |= dsTV_RESOLUTION_720p50;
-                    break;
-                case HDMI_CEA_720p60:
-                    *resolutions |= dsTV_RESOLUTION_720p;
-                    break;
-                case HDMI_CEA_1080p50:
-                    *resolutions |= dsTV_RESOLUTION_1080p50;
-                    break;
-                case HDMI_CEA_1080p24:
-                    *resolutions |= dsTV_RESOLUTION_1080p24;
-                    break;
-                case HDMI_CEA_1080p25:
-                    *resolutions |= dsTV_RESOLUTION_1080p25;
-                    break;
-                case HDMI_CEA_1080p30:
-                    *resolutions |= dsTV_RESOLUTION_1080p30;
-                    break;
-                case HDMI_CEA_1080p60:
-                    *resolutions |= dsTV_RESOLUTION_1080p60;
-                    break;
-                case HDMI_CEA_1080i50:
-                    *resolutions |= dsTV_RESOLUTION_1080i50;
-                    break;
-                case HDMI_CEA_1080i60:
-                    *resolutions |= dsTV_RESOLUTION_1080i;
-                    break;
-                default:
-                    *resolutions |= dsTV_RESOLUTION_480p;
-                    break;
-            }
-#else
             // modeSupported[i].code is VIC
             TVVideoResolution = getResolutionFromVic(modeSupported[i].code);
             if (TVVideoResolution != NULL) {
                 hal_info("VIC %u TVVideoResolution = 0x%x\n", getVicFromResolution(*TVVideoResolution), *TVVideoResolution);
                 *resolutions |= *TVVideoResolution;
             }
-#endif  // Use Mode/VIC Map
         }
     } else {
         hal_err("Get supported resolution for TV on Non HDMI Port\n");
diff --git a/dshalUtils.c b/dshalUtils.c
index e4a8aa0..cf0fa99 100644
--- a/dshalUtils.c
+++ b/dshalUtils.c
@@ -27,19 +27,21 @@
 
 #include "dshalUtils.h"
 #include "dshalLogger.h"
+#include "dsVideoResolutionSettings.h"
 
 const hdmiSupportedRes_t resolutionMap[] = {
     {"480p", 2},       // 720x480p @ 59.94/60Hz
-    {"480p", 3},       // 720x480p @ 59.94/60Hz
+    //{"480p", 3},       // 720x480p @ 59.94/60Hz
     {"480i", 6},       // 720x480i @ 59.94/60Hz
-    {"480i", 7},       // 720x480i @ 59.94/60Hz
+    //{"480i", 7},       // 720x480i @ 59.94/60Hz
     {"576p", 17},      // 720x576p @ 50Hz
-    {"576p", 18},      // 720x576p @ 50Hz
+    //{"576p", 18},      // 720x576p @ 50Hz
     {"576i", 21},      // 720x576i @ 50Hz
-    {"576i", 22},      // 720x576i @ 50Hz
+    //{"576i", 22},      // 720x576i @ 50Hz
     {"720p", 4},       // 1280x720p @ 59.94/60Hz
     {"720p50", 19},    // 1280x720p @ 50Hz
     {"1080i", 5},      // 1920x1080i @ 59.94/60Hz
+	{"1080i60", 5},    // 1920x1080i @ 59.94/60Hz
     {"1080i50", 20},   // 1920x1080i @ 50Hz
     {"1080p24", 32},   // 1920x1080p @ 24Hz
     {"1080p25", 33},   // 1920x1080p @ 25Hz
@@ -50,13 +52,92 @@ const hdmiSupportedRes_t resolutionMap[] = {
     {"2160p25", 94},   // 3840x2160p @ 25Hz
     {"2160p30", 95},   // 3840x2160p @ 30Hz
     {"2160p24", 98},   // 4096x2160p @ 24Hz
-    {"2160p25", 99},   // 4096x2160p @ 25Hz
-    {"2160p30", 100},  // 4096x2160p @ 30Hz
-    {"2160p50", 101},  // 4096x2160p @ 50Hz
-    {"2160p60", 102}   // 4096x2160p @ 60Hz
+    //{"2160p25", 99},   // 4096x2160p @ 25Hz
+    //{"2160p30", 100},  // 4096x2160p @ 30Hz
+    //{"2160p50", 101},  // 4096x2160p @ 50Hz
+    //{"2160p60", 102}   // 4096x2160p @ 60Hz
 };
 
 const size_t  noOfItemsInResolutionMap = sizeof(resolutionMap) / sizeof(hdmiSupportedRes_t);
+const sizze_t noOfItemsInkResolutions = sizeof(kResolutions) / sizeof(dsVideoPortResolution_t);
+
+dsVideoPortResolution_t *dsGetkResolutionByName(const char *name)
+{
+	for (size_t i = 0; i < noOfItemsInkResolutions; i++) {
+		if (strcmp(kResolutions[i].name, name) == 0) {
+			return &kResolutions[i];
+		}
+	}
+	return NULL;
+}
+
+dsVideoPortResolution_t *dsGetkResolutionByPixelResolutionAndFrameRate(dsVideoPortPixelResolution_t pixelResolution, dsVideoPortFrameRate_t frameRate)
+{
+	for (size_t i = 0; i < noOfItemsInkResolutions; i++) {
+		if ((kResolutions[i].pixelResolution == pixelResolution) && (kResolutions[i].frameRate == frameRate)) {
+			return &kResolutions[i];
+		}
+	}
+	return NULL;
+}
+
+bool convertWesterosResolutionTokResolution(const char *westerosRes, dsVideoPortResolution_t *kResolution)
+{
+	if (westerosRes == NULL || resolution == NULL) {
+		return false;
+	}
+	int Width = 0;
+	int Height = 0;
+	int FrameRate = 0;
+	if (sscanf(wstresolution, "%dx%dpx%d", &Width, &Height, &FrameRate) == 3 ||
+			sscanf(wstresolution, "%dx%dix%d", &Width, &Height, &FrameRate) == 3) {
+		snprintf(sFrameRate, sizeof(sFrameRate), "%d", FrameRate);
+		hal_dbg("Width: %d, Height: %d, FrameRate: %d\n", Width, Height, FrameRate);
+		kResolution->pixelResolution = getdsVideoResolution(Width, Height);
+		if (kResolution->pixelResolution != dsVIDEO_PIXELRES_MAX) {
+			kResolution->frameRate = getdsVideoFrameRate(FrameRate);
+			kResolution = dsGetkResolutionByPixelResolutionAndFrameRate(kResolution->pixelResolution, kResolution->frameRate);
+			return (kResolution != NULL) ? true : false;
+		}
+	}
+	return false;
+}
+
+bool convertkResolutionToWesterosResolution(const dsVideoPortResolution_t *kResolution, char *westerosRes, size_t size)
+{
+	if (kResolution == NULL || westerosRes == NULL) {
+		return false;
+	}
+	// get from westerosReskResMap
+	for (size_t i = 0; i < sizeof(westerosReskResMap) / sizeof(WesterosReskResMap_t); i++) {
+		if (strcmp(kResolution->name, westerosReskResMap[i].dsVideoPortResolutionName) == 0) {
+			snprintf(westerosRes, size, "%s", westerosReskResMap[i].westerosRes);
+			return true;
+		}
+	}
+	return false;
+}
+
+const WesterosReskResMap_t westerosReskResMap[] = {
+	{"720x480px60", "480p"},
+	{"720x480ix60", "480i"},
+	{"720x576px50", "576p"},
+	{"720x576ix50", "576i"},
+	{"1280x720px60", "720p"},
+	{"1280x720px50", "720p50"},
+	{"1920x1080p24", "1080p24"},
+	{"1920x1080p25", "1080p25"},
+	{"1920x1080p30", "1080p30"},
+	{"1920x1080p50", "1080p50"},
+	{"1920x1080p60", "1080p60"},
+	{"1920x1080ix60", "1080i"},
+	{"1920x1080ix50", "1080i50"},
+	{"3840x2160px24", "2160p24"},
+	{"3840x2160px25", "2160p25"},
+	{"3840x2160px30", "2160p30"},
+	{"3840x2160px50", "2160p50"},
+	{"3840x2160px60", "2160p60"}
+};
 
 const VicMapEntry vicMapTable[] = {
     // 480i resolutions
@@ -161,6 +242,21 @@ const VicMapEntry vicMapTable[] = {
 
 #define VIC_MAP_TABLE_SIZE (sizeof(vicMapTable) / sizeof(VicMapEntry))
 
+char *getdsVideoFrameRateString(dsVideoFrameRate_t framerate)
+{
+	switch (framerate) {
+		case dsVIDEO_FRAMERATE_24: return "24";
+		case dsVIDEO_FRAMERATE_25: return "25";
+		case dsVIDEO_FRAMERATE_30: return "30";
+		case dsVIDEO_FRAMERATE_60: return "60";
+		case dsVIDEO_FRAMERATE_23dot98: return "23.98";
+		case dsVIDEO_FRAMERATE_29dot97: return "29.97";
+		case dsVIDEO_FRAMERATE_50: return "50";
+		case dsVIDEO_FRAMERATE_59dot94: return "59.94";
+		default: return NULL;
+	}
+}
+
 dsVideoFrameRate_t getdsVideoFrameRate(uint16_t frameRate)
 {
 	switch (frameRate) {
@@ -491,15 +587,15 @@ bool westerosRWWrapper(const char *cmd, char *resp, size_t respSize)
     return false;
 }
 
-const char *getWesterosResolutionFromVic(int vic)
-{
-    for (size_t i = 0; i < VIC_MAP_TABLE_SIZE; ++i) {
-        if (vicMapTable[i].vic == vic) {
-            return vicMapTable[i].westerosResolution;
-        }
-    }
-    return NULL; // VIC not found
-}
+// const char *getWesterosResolutionFromVic(int vic)
+// {
+//     for (size_t i = 0; i < VIC_MAP_TABLE_SIZE; ++i) {
+//         if (vicMapTable[i].vic == vic) {
+//             return vicMapTable[i].westerosResolution;
+//         }
+//     }
+//     return NULL; // VIC not found
+// }
 
 const dsTVResolution_t *getResolutionFromVic(int vic)
 {
@@ -511,12 +607,12 @@ const dsTVResolution_t *getResolutionFromVic(int vic)
     return NULL; // VIC not found
 }
 
-const int *getVicFromResolution(dsTVResolution_t resolution)
-{
-    for (size_t i = 0; i < VIC_MAP_TABLE_SIZE; ++i) {
-        if (vicMapTable[i].tvresolution == resolution) {
-            return &vicMapTable[i].vic;
-        }
-    }
-    return NULL;  // VIC not found
-}
+// const int *getVicFromResolution(dsTVResolution_t resolution)
+// {
+//     for (size_t i = 0; i < VIC_MAP_TABLE_SIZE; ++i) {
+//         if (vicMapTable[i].tvresolution == resolution) {
+//             return &vicMapTable[i].vic;
+//         }
+//     }
+//     return NULL;  // VIC not found
+// }
diff --git a/dshalUtils.h b/dshalUtils.h
index 7f0119c..f372071 100644
--- a/dshalUtils.h
+++ b/dshalUtils.h
@@ -24,6 +24,12 @@
 #include "interface/vmcs_host/vc_vchi_gencmd.h"
 #include "dsTypes.h"
 #include "dsAVDTypes.h"
+#include "dsVideoDeviceSettings.h"
+
+typedef struct {
+	const char *dsVideoPortResolutionName;
+	const char *westerosResolution;
+} WesterosReskResMap_t;
 
 typedef struct {
     int vic;
@@ -65,8 +71,8 @@ void parse_edid(const uint8_t *edid, EDID_t *parsed_edid);
 void print_edid(const EDID_t *parsed_edid);
 bool westerosRWWrapper(const char *cmd, char *resp, size_t respSize);
 const dsTVResolution_t *getResolutionFromVic(int vic);
-const char *getWesterosResolutionFromVic(int vic);
-const int *getVicFromResolution(dsTVResolution_t resolution);
+//const char *getWesterosResolutionFromVic(int vic);
+//const int *getVicFromResolution(dsTVResolution_t resolution);
 dsVideoResolution_t getdsVideoResolution(uint32_t width, uint32_t height);
 dsVideoAspectRatio_t getdsVideoAspectRatio(uint16_t aspectRatio);
 dsVideoAspectRatio_t getAspectRatioFromWidthHeight(int width, int height);
