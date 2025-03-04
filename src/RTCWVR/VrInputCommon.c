/************************************************************************************

Filename	:	VrInputRight.c 
Content		:	Handles common controller input functionality
Created		:	September 2019
Authors		:	Simon Brown

*************************************************************************************/

//#include <VrApi.h>
//#include <VrApi_Helpers.h>
//#include <VrApi_SystemUtils.h>
//#include <VrApi_Input.h>
//#include <VrApi_Types.h>

#include "VrInput.h"

#include "../qcommon/qcommon.h"
// CACTUS
#include "../Oculus/Include/OVR_Platform.h"
#include "../Oculus/Include/OVR_CAPI.h"
//keys.h
void Sys_QueEvent( int time, sysEventType_t type, int value, int value2, int ptrLength, void *ptr );
void handleTrackedControllerButton(ovrInputState * trackedRemoteState, ovrInputState * prevTrackedRemoteState, uint32_t button, int key)
{
    if ((trackedRemoteState->Buttons & button) != (prevTrackedRemoteState->Buttons & button))
    {
        Sys_QueEvent( 0, SE_KEY, key, (trackedRemoteState->Buttons & button) != 0, 0, NULL );
//        Key_Event(key, (trackedRemoteState->Buttons & button) != 0, global_time);
    }
}

void rotateAboutOrigin(float x, float y, float rotation, vec2_t out)
{
    out[0] = cosf(DEG2RAD(-rotation)) * x  +  sinf(DEG2RAD(-rotation)) * y;
    out[1] = cosf(DEG2RAD(-rotation)) * y  -  sinf(DEG2RAD(-rotation)) * x;
}

float length(float x, float y)
{
    return sqrtf(powf(x, 2.0f) + powf(y, 2.0f));
}

#define NLF_DEADZONE 0.1
#define NLF_POWER 2.2

float nonLinearFilter(float in)
{
    float val = 0.0f;
    if (in > NLF_DEADZONE)
    {
        val = in;
        val -= NLF_DEADZONE;
        val /= (1.0f - NLF_DEADZONE);
        val = powf(val, NLF_POWER);
    }
    else if (in < -NLF_DEADZONE)
    {
        val = in;
        val += NLF_DEADZONE;
        val /= (1.0f - NLF_DEADZONE);
        val = -powf(fabsf(val), NLF_POWER);
    }

    return val;
}

void sendButtonActionSimple(const char* action)
{
    char command[256];
    snprintf( command, sizeof( command ), "%s\n", action );
    Cbuf_AddText( command );
}

qboolean between(float min, float val, float max)
{
    return (min < val) && (val < max);
}

void sendButtonAction(const char* action, long buttonDown)
{
    char command[256];
    snprintf( command, sizeof( command ), "%s\n", action );
    if (!buttonDown)
    {
        command[0] = '-';
    }
    Cbuf_AddText( command );
}

void acquireTrackedRemotesData(const ovrMobile *Ovr, double displayTime) {//The amount of yaw changed by controller
    for ( int i = 0; ; i++ ) {
        ovrInputCapabilityHeader cap;
        ovrResult result = vrapi_EnumerateInputDevices(Ovr, i, &cap);
        if (result < 0) {
            break;
        }

        if (cap.Type == ovrControllerType_TrackedRemote) {
            ovrTracking remoteTracking;
            ovrInputStateTrackedRemote trackedRemoteState;
            trackedRemoteState.Header.ControllerType = ovrControllerType_TrackedRemote;
            result = vrapi_GetCurrentInputState(Ovr, cap.DeviceID, &trackedRemoteState.Header);

            if (result == ovrSuccess) {
                ovrInputTrackedRemoteCapabilities remoteCapabilities;
                remoteCapabilities.Header = cap;
                result = vrapi_GetInputDeviceCapabilities(Ovr, &remoteCapabilities.Header);

                result = vrapi_GetInputTrackingState(Ovr, cap.DeviceID, displayTime,
                                                     &remoteTracking);

                if (remoteCapabilities.ControllerCapabilities & ovrControllerCaps_RightHand) {
                    rightTrackedRemoteState_new = trackedRemoteState;
                    rightRemoteTracking_new = remoteTracking;
                    controllerIDs[1] = cap.DeviceID;
                } else{
                    leftTrackedRemoteState_new = trackedRemoteState;
                    leftRemoteTracking_new = remoteTracking;
                    controllerIDs[0] = cap.DeviceID;
                }
            }
        }
    }
}


//YAW:  Left increase, Right decrease
void updateScopeAngles()
{
    //Bit of a hack, but use weapon orientation / position for view when scope is engaged
    static vec3_t currentScopeAngles;
    static vec3_t lastScopeAngles;
    if (vr.scopeengaged)
    {
        //Clear weapon offset
        VectorSet(vr.weaponoffset, 0, 0, 0);

        VectorSet(currentScopeAngles, vr.weaponangles[PITCH], vr.weaponangles[YAW], vr.hmdorientation[ROLL]);

        //Set "view" Angles
        VectorCopy(currentScopeAngles, vr.hmdorientation);

        //Orientation
        VectorSubtract(lastScopeAngles, currentScopeAngles, vr.hmdorientation_delta);

        //Keep this for our records
        VectorCopy(currentScopeAngles, lastScopeAngles);
    } else {
        VectorSet(currentScopeAngles, vr.weaponangles[PITCH], vr.weaponangles[YAW], vr.hmdorientation[ROLL]);
        VectorCopy(currentScopeAngles, lastScopeAngles);
    }
}

void PortableMouseAbs(float x,float y);
inline float clamp(float _min, float _val, float _max)
{
    return max(min(_val, _max), _min);
}

void interactWithTouchScreen(qboolean reset, ovrInputStateTrackedRemote *newState, ovrInputStateTrackedRemote *oldState) {
    static float cursorX = 0.25f;
    static float cursorY = 0.125f;

    if (reset)
    {
        cursorX = 0.25f;
        cursorY = 0.125f;
    }

    cursorX += (float)(vr.weaponangles_delta[YAW] / 180.0);
    cursorX = clamp(0.0, cursorX, 0.5);
    cursorY += (float)(-vr.weaponangles_delta[PITCH] / 220.0);
    cursorY = clamp(0.0, cursorY, 0.4);

    PortableMouseAbs(cursorX, cursorY);
}