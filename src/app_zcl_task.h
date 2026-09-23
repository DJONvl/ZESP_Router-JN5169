/**
 * @file  app_zcl_task.h
 * @brief ZCL Interface
 */

#ifndef APP_ZCL_TASK_H
#define APP_ZCL_TASK_H

#include <jendefs.h>

/* SDK JN-SW-4170 */
#include "Basic.h"
#include "Identify.h"
#include "OnOff.h"
#include "LevelControl.h"
#include "ColourControl.h"
#include "MultistateOutputBasic.h"
#include "IlluminanceMeasurement.h"
#include "zcl.h"

/* EP1: router + virtual RGB lamp */
typedef struct {
    tsZCL_ClusterInstance sBasicServer;
    tsZCL_ClusterInstance sIdentifyServer;
    tsZCL_ClusterInstance sOnOffServer;
    tsZCL_ClusterInstance sLevelControlServer;
    tsZCL_ClusterInstance sColourControlServer;

} APP_tsLumiRouterClusterInstances __attribute__((aligned(4)));

typedef struct {
    tsZCL_EndPointDefinition sEndPoint;

    /* Cluster instances */
    APP_tsLumiRouterClusterInstances sClusterInstance;

    /* Basic Cluster - Server */
    tsCLD_Basic sBasicServerCluster;

    /* Identify Cluster - Server */
    tsCLD_Identify sIdentifyServerCluster;
    tsCLD_IdentifyCustomDataStructure sIdentifyServerCustomDataStructure;

    /* On/Off Cluster - Server (lamp) */
    tsCLD_OnOff sOnOffServerCluster;
    tsCLD_OnOffCustomDataStructure sOnOffServerCustomDataStructure;

    /* Level Control Cluster - Server (lamp brightness) */
    tsCLD_LevelControl sLevelControlServerCluster;
    tsCLD_LevelControlCustomDataStructure sLevelControlServerCustomDataStructure;

    /* Colour Control Cluster - Server (lamp colour) */
    tsCLD_ColourControl sColourControlServerCluster;
    tsCLD_ColourControlCustomDataStructure sColourControlServerCustomDataStructure;

} APP_tsLumiRouter;

/* EP2: virtual light sensor */
typedef struct {
    tsZCL_ClusterInstance sIlluminanceMeasurementServer;

} APP_tsSensorClusterInstances __attribute__((aligned(4)));

typedef struct {
    tsZCL_EndPointDefinition sEndPoint;

    /* Cluster instances */
    APP_tsSensorClusterInstances sClusterInstance;

    /* Illuminance Measurement Cluster - Server */
    tsCLD_IlluminanceMeasurement sIlluminanceMeasurementServerCluster;

} APP_tsSensor;

/* EP3: virtual doorbell (play/mute + volume + melody) */
typedef struct {
    tsZCL_ClusterInstance sOnOffServer;
    tsZCL_ClusterInstance sLevelControlServer;
    tsZCL_ClusterInstance sMultistateOutputServer;

} APP_tsDoorbellClusterInstances __attribute__((aligned(4)));

typedef struct {
    tsZCL_EndPointDefinition sEndPoint;

    /* Cluster instances */
    APP_tsDoorbellClusterInstances sClusterInstance;

    /* On/Off Cluster - Server (play/stop) */
    tsCLD_OnOff sOnOffServerCluster;
    tsCLD_OnOffCustomDataStructure sOnOffServerCustomDataStructure;

    /* Level Control Cluster - Server (volume) */
    tsCLD_LevelControl sLevelControlServerCluster;
    tsCLD_LevelControlCustomDataStructure sLevelControlServerCustomDataStructure;

    /* Multistate Output Cluster - Server (melody select) */
    tsCLD_MultistateOutputBasic sMultistateOutputServerCluster;

} APP_tsDoorbell;

extern PUBLIC APP_tsLumiRouter sLumiRouter;
extern PUBLIC APP_tsSensor sSensor;
extern PUBLIC APP_tsDoorbell sDoorbell;

PUBLIC void APP_ZCL_vInitialise(void);
PUBLIC void APP_ZCL_vEventHandler(ZPS_tsAfEvent *psStackEvent);
PUBLIC void APP_cbTimerZclTick(void *pvParam);

#endif /* APP_ZCL_TASK_H */
