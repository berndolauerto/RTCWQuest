/************************************************************************************

Filename	:	RTCWVR_SurfaceView.c based on VrCubeWorld_SurfaceView.c
Content		:	This sample uses a plain Android SurfaceView and handles all
				Activity and Surface life cycle events in native code. This sample
				does not use the application framework and also does not use LibOVR.
				This sample only uses the VrApi.
Created		:	March, 2015
Authors		:	J.M.P. van Waveren / Simon Brown

Copyright	:	Copyright 2015 Oculus VR, LLC. All Rights reserved.

*************************************************************************************/

#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <time.h>
#include "unistd.h"				// Cactus
#include "../pthreadsw32/include/pthread.h" 
// #include <sys/prctl.h>					// for prctl( PR_SET_NAME )
//#include <android/log.h>
//#include <android/native_window_jni.h>	// for native window JNI
#include <windows.h>		// Cactus - Android Stuff

#include "argtable3.h"
#include "VrInput.h"
#include "VrCvars.h"

 #include "egl.h"				// Cactus - Android Stuff
 #include "eglext.h"			// Cactus - Android Stuff
 #include "GLES3/gl3.h"			// Cactus - Android Stuff
 #include "GLES3/gl3ext.h"// Cactus - Android Stuff



// #include "VrApi.h"			// Cactus - Android Stuff
// #include "VrApi_Helpers.h"		// Cactus - Android Stuff
// #include "VrApi_SystemUtils.h"		// Cactus - Android Stuff
// #include "VrApi_Input.h"				// Cactus - Android Stuff
// #include "VrApi_Types.h"				// Cactus - Android Stuff

// #include <src/gl/loader.h>		// Cactus - Android Stuff

//#include <SDL2/SDL.h>		
//#include <SDL2/SDL_main.h>
#include "../client/client.h""

#include "VrCompositor.h"
#include "VrInput.h"


// CACTUS
#include "../Oculus/Include/OVR_Platform.h"
#include "../Oculus/Include/OVR_CAPI.h"

#if !defined( EGL_OPENGL_ES3_BIT_KHR )
#define EGL_OPENGL_ES3_BIT_KHR		0x0040
#endif

// EXT_texture_border_clamp
#ifndef GL_CLAMP_TO_BORDER
#define GL_CLAMP_TO_BORDER			0x812D
#endif

#ifndef GL_TEXTURE_BORDER_COLOR
#define GL_TEXTURE_BORDER_COLOR		0x1004
#endif


// Must use EGLSyncKHR because the VrApi still supports OpenGL ES 2.0
#define EGL_SYNC

#if defined EGL_SYNC
// EGL_KHR_reusable_sync
PFNEGLCREATESYNCKHRPROC			eglCreateSyncKHR;
PFNEGLDESTROYSYNCKHRPROC		eglDestroySyncKHR;
PFNEGLCLIENTWAITSYNCKHRPROC		eglClientWaitSyncKHR;
PFNEGLSIGNALSYNCKHRPROC			eglSignalSyncKHR;
PFNEGLGETSYNCATTRIBKHRPROC		eglGetSyncAttribKHR;
#endif

//Let's go to the maximum!
int CPU_LEVEL			= 4;
int GPU_LEVEL			= 4;
int NUM_MULTI_SAMPLES	= 1;
float SS_MULTIPLIER    = 1.25f;

//jclass clazz;

float radians(float deg) {
	return (deg * M_PI) / 180.0;
}

float degrees(float rad) {
	return (rad * 180.0) / M_PI;
}

/* global arg_xxx structs */
struct arg_dbl *ss;
struct arg_int *cpu;
struct arg_int *gpu;
struct arg_end *end;

char **argv;
int argc=0;

extern cvar_t	*r_lefthand;
extern cvar_t   *cl_paused;

enum control_scheme {
	RIGHT_HANDED_DEFAULT = 0,
	LEFT_HANDED_DEFAULT = 10,
	WEAPON_ALIGN = 99
};

/*
================================================================================

System Clock Time in millis

================================================================================
*/

double GetTimeInMilliSeconds()
{
	struct timespec now;
	clock_gettime( CLOCK_MONOTONIC, &now );
	return ( now.tv_sec * 1e9 + now.tv_nsec ) * (double)(1e-6);
}

/*
================================================================================

LAMBDA1VR Stuff

================================================================================
*/

qboolean RTCWVR_useScreenLayer()
{
	vr.screen = (qboolean)(showingScreenLayer ||
            (cls.state == CA_CINEMATIC) ||
            (cls.state == CA_LOADING) ||
            (clc.demoplaying) ||
			(cl.cameraMode) ||
            ( Key_GetCatcher( ) & KEYCATCH_UI ) ||
            ( Key_GetCatcher( ) & KEYCATCH_CONSOLE ));

	return vr.screen;
}

int runStatus = -1;
void RTCWVR_exit(int exitCode)
{
	runStatus = exitCode;
}

static void UnEscapeQuotes( char *arg )
{
	char *last = NULL;
	while( *arg ) {
		if( *arg == '"' && *last == '\\' ) {
			char *c_curr = arg;
			char *c_last = last;
			while( *c_curr ) {
				*c_last = *c_curr;
				c_last = c_curr;
				c_curr++;
			}
			*c_last = '\0';
		}
		last = arg;
		arg++;
	}
}

static int ParseCommandLine(char *cmdline, char **argv)
{
	char *bufp;
	char *lastp = NULL;
	int argc, last_argc;
	argc = last_argc = 0;
	for ( bufp = cmdline; *bufp; ) {
		while ( isspace(*bufp) ) {
			++bufp;
		}
		if ( *bufp == '"' ) {
			++bufp;
			if ( *bufp ) {
				if ( argv ) {
					argv[argc] = bufp;
				}
				++argc;
			}
			while ( *bufp && ( *bufp != '"' || *lastp == '\\' ) ) {
				lastp = bufp;
				++bufp;
			}
		} else {
			if ( *bufp ) {
				if ( argv ) {
					argv[argc] = bufp;
				}
				++argc;
			}
			while ( *bufp && ! isspace(*bufp) ) {
				++bufp;
			}
		}
		if ( *bufp ) {
			if ( argv ) {
				*bufp = '\0';
			}
			++bufp;
		}
		if( argv && last_argc != argc ) {
			UnEscapeQuotes( argv[last_argc] );
		}
		last_argc = argc;
	}
	if ( argv ) {
		argv[argc] = NULL;
	}
	return(argc);
}

/*
================================================================================

OpenGL-ES Utility Functions

================================================================================
*/

typedef struct
{
	bool multi_view;						// GL_OVR_multiview, GL_OVR_multiview2
	bool EXT_texture_border_clamp;			// GL_EXT_texture_border_clamp, GL_OES_texture_border_clamp
} OpenGLExtensions_t;

OpenGLExtensions_t glExtensions;

static void EglInitExtensions()
{
#if defined EGL_SYNC
	eglCreateSyncKHR		= (PFNEGLCREATESYNCKHRPROC)			eglGetProcAddress( "eglCreateSyncKHR" );
	eglDestroySyncKHR		= (PFNEGLDESTROYSYNCKHRPROC)		eglGetProcAddress( "eglDestroySyncKHR" );
	eglClientWaitSyncKHR	= (PFNEGLCLIENTWAITSYNCKHRPROC)		eglGetProcAddress( "eglClientWaitSyncKHR" );
	eglSignalSyncKHR		= (PFNEGLSIGNALSYNCKHRPROC)			eglGetProcAddress( "eglSignalSyncKHR" );
	eglGetSyncAttribKHR		= (PFNEGLGETSYNCATTRIBKHRPROC)		eglGetProcAddress( "eglGetSyncAttribKHR" );
#endif

	const char * allExtensions = (const char *)glGetString( GL_EXTENSIONS );
	if ( allExtensions != NULL )
	{
		glExtensions.multi_view = strstr( allExtensions, "GL_OVR_multiview2" ) &&
								  strstr( allExtensions, "GL_OVR_multiview_multisampled_render_to_texture" );

		glExtensions.EXT_texture_border_clamp = false;//strstr( allExtensions, "GL_EXT_texture_border_clamp" ) ||
												//strstr( allExtensions, "GL_OES_texture_border_clamp" );
	}
}

static const char * EglErrorString( const EGLint error )
{
	switch ( error )
	{
		case EGL_SUCCESS:				return "EGL_SUCCESS";
		case EGL_NOT_INITIALIZED:		return "EGL_NOT_INITIALIZED";
		case EGL_BAD_ACCESS:			return "EGL_BAD_ACCESS";
		case EGL_BAD_ALLOC:				return "EGL_BAD_ALLOC";
		case EGL_BAD_ATTRIBUTE:			return "EGL_BAD_ATTRIBUTE";
		case EGL_BAD_CONTEXT:			return "EGL_BAD_CONTEXT";
		case EGL_BAD_CONFIG:			return "EGL_BAD_CONFIG";
		case EGL_BAD_CURRENT_SURFACE:	return "EGL_BAD_CURRENT_SURFACE";
		case EGL_BAD_DISPLAY:			return "EGL_BAD_DISPLAY";
		case EGL_BAD_SURFACE:			return "EGL_BAD_SURFACE";
		case EGL_BAD_MATCH:				return "EGL_BAD_MATCH";
		case EGL_BAD_PARAMETER:			return "EGL_BAD_PARAMETER";
		case EGL_BAD_NATIVE_PIXMAP:		return "EGL_BAD_NATIVE_PIXMAP";
		case EGL_BAD_NATIVE_WINDOW:		return "EGL_BAD_NATIVE_WINDOW";
		case EGL_CONTEXT_LOST:			return "EGL_CONTEXT_LOST";
		default:						return "unknown";
	}
}

static const char * GlFrameBufferStatusString( GLenum status )
{
	switch ( status )
	{
		case GL_FRAMEBUFFER_UNDEFINED:						return "GL_FRAMEBUFFER_UNDEFINED";
		case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:			return "GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT";
		case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:	return "GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT";
		case GL_FRAMEBUFFER_UNSUPPORTED:					return "GL_FRAMEBUFFER_UNSUPPORTED";
		case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE:			return "GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE";
		default:											return "unknown";
	}
}


/*
================================================================================

ovrEgl

================================================================================
*/

typedef struct
{
	EGLint		MajorVersion;
	EGLint		MinorVersion;
	EGLDisplay	Display;
	EGLConfig	Config;
	EGLSurface	TinySurface;
	EGLSurface	MainSurface;
	EGLContext	Context;
} ovrEgl;

static void ovrEgl_Clear( ovrEgl * egl )
{
	egl->MajorVersion = 0;
	egl->MinorVersion = 0;
	egl->Display = 0;
	egl->Config = 0;
	egl->TinySurface = EGL_NO_SURFACE;
	egl->MainSurface = EGL_NO_SURFACE;
	egl->Context = EGL_NO_CONTEXT;
}

static void ovrEgl_CreateContext( ovrEgl * egl, const ovrEgl * shareEgl )
{
	if ( egl->Display != 0 )
	{
		return;
	}

	egl->Display = eglGetDisplay( EGL_DEFAULT_DISPLAY );
	ALOGV( "        eglInitialize( Display, &MajorVersion, &MinorVersion )" );
	eglInitialize( egl->Display, &egl->MajorVersion, &egl->MinorVersion );
	// Do NOT use eglChooseConfig, because the Android EGL code pushes in multisample
	// flags in eglChooseConfig if the user has selected the "force 4x MSAA" option in
	// settings, and that is completely wasted for our warp target.
	const int MAX_CONFIGS = 1024;
	EGLConfig configs[MAX_CONFIGS];
	EGLint numConfigs = 0;
	if ( eglGetConfigs( egl->Display, configs, MAX_CONFIGS, &numConfigs ) == EGL_FALSE )
	{
		//ALOGE( "        eglGetConfigs() failed: %s", EglErrorString( eglGetError() ) );
		return;
	}
	const EGLint configAttribs[] =
	{
		EGL_RED_SIZE,		8,
		EGL_GREEN_SIZE,		8,
		EGL_BLUE_SIZE,		8,
		EGL_ALPHA_SIZE,		8, // need alpha for the multi-pass timewarp compositor
		EGL_DEPTH_SIZE,		0,
		EGL_STENCIL_SIZE,	0,
		EGL_SAMPLES,		0,
		EGL_NONE
	};
	egl->Config = 0;
	for ( int i = 0; i < numConfigs; i++ )
	{
		EGLint value = 0;

		eglGetConfigAttrib( egl->Display, configs[i], EGL_RENDERABLE_TYPE, &value );
		if ( ( value & EGL_OPENGL_ES3_BIT_KHR ) != EGL_OPENGL_ES3_BIT_KHR )
		{
			continue;
		}

		// The pbuffer config also needs to be compatible with normal window rendering
		// so it can share textures with the window context.
		eglGetConfigAttrib( egl->Display, configs[i], EGL_SURFACE_TYPE, &value );
		if ( ( value & ( EGL_WINDOW_BIT | EGL_PBUFFER_BIT ) ) != ( EGL_WINDOW_BIT | EGL_PBUFFER_BIT ) )
		{
			continue;
		}

		int	j = 0;
		for ( ; configAttribs[j] != EGL_NONE; j += 2 )
		{
			eglGetConfigAttrib( egl->Display, configs[i], configAttribs[j], &value );
			if ( value != configAttribs[j + 1] )
			{
				break;
			}
		}
		if ( configAttribs[j] == EGL_NONE )
		{
			egl->Config = configs[i];
			break;
		}
	}
	if ( egl->Config == 0 )
	{
		ALOGE( "        eglChooseConfig() failed: %s", EglErrorString( eglGetError() ) );
		return;
	}
	EGLint contextAttribs[] =
	{
		EGL_CONTEXT_CLIENT_VERSION, 3,
		EGL_NONE
	};
	ALOGV( "        Context = eglCreateContext( Display, Config, EGL_NO_CONTEXT, contextAttribs )" );
	egl->Context = eglCreateContext( egl->Display, egl->Config, ( shareEgl != NULL ) ? shareEgl->Context : EGL_NO_CONTEXT, contextAttribs );
	if ( egl->Context == EGL_NO_CONTEXT )
	{
		//ALOGE( "        eglCreateContext() failed: %s", EglErrorString( eglGetError() ) );
		return;
	}
	const EGLint surfaceAttribs[] =
	{
		EGL_WIDTH, 16,
		EGL_HEIGHT, 16,
		EGL_NONE
	};
	ALOGV( "        TinySurface = eglCreatePbufferSurface( Display, Config, surfaceAttribs )" );
	egl->TinySurface = eglCreatePbufferSurface( egl->Display, egl->Config, surfaceAttribs );
	if ( egl->TinySurface == EGL_NO_SURFACE )
	{
		//ALOGE( "        eglCreatePbufferSurface() failed: %s", EglErrorString( eglGetError() ) );
		eglDestroyContext( egl->Display, egl->Context );
		egl->Context = EGL_NO_CONTEXT;
		return;
	}
	ALOGV( "        eglMakeCurrent( Display, TinySurface, TinySurface, Context )" );
	if ( eglMakeCurrent( egl->Display, egl->TinySurface, egl->TinySurface, egl->Context ) == EGL_FALSE )
	{
		//ALOGE( "        eglMakeCurrent() failed: %s", EglErrorString( eglGetError() ) );
		eglDestroySurface( egl->Display, egl->TinySurface );
		eglDestroyContext( egl->Display, egl->Context );
		egl->Context = EGL_NO_CONTEXT;
		return;
	}
}

static void ovrEgl_DestroyContext( ovrEgl * egl )
{
	if ( egl->Display != 0 )
	{
		ALOGE( "        eglMakeCurrent( Display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT )" );
		if ( eglMakeCurrent( egl->Display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT ) == EGL_FALSE )
		{
			ALOGE( "        eglMakeCurrent() failed: %s", EglErrorString( eglGetError() ) );
		}
	}
	if ( egl->Context != EGL_NO_CONTEXT )
	{
		ALOGE( "        eglDestroyContext( Display, Context )" );
		if ( eglDestroyContext( egl->Display, egl->Context ) == EGL_FALSE )
		{
			ALOGE( "        eglDestroyContext() failed: %s", EglErrorString( eglGetError() ) );
		}
		egl->Context = EGL_NO_CONTEXT;
	}
	if ( egl->TinySurface != EGL_NO_SURFACE )
	{
		//ALOGE( "        eglDestroySurface( Display, TinySurface )" );
		if ( eglDestroySurface( egl->Display, egl->TinySurface ) == EGL_FALSE )
		{
			ALOGE( "        eglDestroySurface() failed: %s", EglErrorString( eglGetError() ) );
		}
		egl->TinySurface = EGL_NO_SURFACE;
	}
	if ( egl->Display != 0 )
	{
		ALOGE( "        eglTerminate( Display )" );
		if ( eglTerminate( egl->Display ) == EGL_FALSE )
		{
			ALOGE( "        eglTerminate() failed: %s", EglErrorString( eglGetError() ) );
		}
		egl->Display = 0;
	}
}

/*
================================================================================

ovrFramebuffer

================================================================================
*/


static void ovrFramebuffer_Clear( ovrFramebuffer * frameBuffer )
{
	frameBuffer->Width = 0;
	frameBuffer->Height = 0;
	frameBuffer->Multisamples = 0;
	frameBuffer->TextureSwapChainLength = 0;
	frameBuffer->ProcessingTextureSwapChainIndex = 0;
	frameBuffer->ReadyTextureSwapChainIndex = 0;
	frameBuffer->ColorTextureSwapChain = NULL;
	frameBuffer->DepthBuffers = NULL;
	frameBuffer->FrameBuffers = NULL;
}

static bool ovrFramebuffer_Create( ovrFramebuffer * frameBuffer, const GLenum colorFormat, const int width, const int height, const int multisamples )
{
    LOAD_GLES2(glBindTexture);
    LOAD_GLES2(glTexParameteri);
    LOAD_GLES2(glGenRenderbuffers);
    LOAD_GLES2(glBindRenderbuffer);
    LOAD_GLES2(glRenderbufferStorage);
    LOAD_GLES2(glGenFramebuffers);
    LOAD_GLES2(glBindFramebuffer);
    LOAD_GLES2(glFramebufferRenderbuffer);
    LOAD_GLES2(glFramebufferTexture2D);
    LOAD_GLES2(glCheckFramebufferStatus);

    frameBuffer->Width = width;
	frameBuffer->Height = height;
	frameBuffer->Multisamples = multisamples;

	frameBuffer->ColorTextureSwapChain = vrapi_CreateTextureSwapChain3( VRAPI_TEXTURE_TYPE_2D, colorFormat, frameBuffer->Width, frameBuffer->Height, 1, 3 );
	frameBuffer->TextureSwapChainLength = vrapi_GetTextureSwapChainLength( frameBuffer->ColorTextureSwapChain );
	frameBuffer->DepthBuffers = (GLuint *)malloc( frameBuffer->TextureSwapChainLength * sizeof( GLuint ) );
	frameBuffer->FrameBuffers = (GLuint *)malloc( frameBuffer->TextureSwapChainLength * sizeof( GLuint ) );

	for ( int i = 0; i < frameBuffer->TextureSwapChainLength; i++ )
	{
		// Create the color buffer texture.
		const GLuint colorTexture = vrapi_GetTextureSwapChainHandle( frameBuffer->ColorTextureSwapChain, i );
		GLenum colorTextureTarget = GL_TEXTURE_2D;
		GL( gles_glBindTexture( colorTextureTarget, colorTexture ) );
        // Just clamp to edge. However, this requires manually clearing the border
        // around the layer to clear the edge texels.
        GL( gles_glTexParameteri( colorTextureTarget, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE ) );
        GL( gles_glTexParameteri( colorTextureTarget, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE ) );

		GL( gles_glTexParameteri( colorTextureTarget, GL_TEXTURE_MIN_FILTER, GL_LINEAR ) );
		GL( gles_glTexParameteri( colorTextureTarget, GL_TEXTURE_MAG_FILTER, GL_LINEAR ) );
		GL( gles_glBindTexture( colorTextureTarget, 0 ) );

		{
			{
				// Create depth buffer.
				GL( gles_glGenRenderbuffers( 1, &frameBuffer->DepthBuffers[i] ) );
				GL( gles_glBindRenderbuffer( GL_RENDERBUFFER, frameBuffer->DepthBuffers[i] ) );
				GL( gles_glRenderbufferStorage( GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, frameBuffer->Width, frameBuffer->Height ) );
				GL( gles_glBindRenderbuffer( GL_RENDERBUFFER, 0 ) );

				// Create the frame buffer.
				GL( gles_glGenFramebuffers( 1, &frameBuffer->FrameBuffers[i] ) );
				GL( gles_glBindFramebuffer( GL_DRAW_FRAMEBUFFER, frameBuffer->FrameBuffers[i] ) );
				GL( gles_glFramebufferRenderbuffer( GL_DRAW_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, frameBuffer->DepthBuffers[i] ) );
				GL( gles_glFramebufferTexture2D( GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorTexture, 0 ) );
				GL( GLenum renderFramebufferStatus = gles_glCheckFramebufferStatus( GL_DRAW_FRAMEBUFFER ) );
				GL( gles_glBindFramebuffer( GL_DRAW_FRAMEBUFFER, 0 ) );
				if ( renderFramebufferStatus != GL_FRAMEBUFFER_COMPLETE )
				{
					ALOGE( "Incomplete frame buffer object: %s", GlFrameBufferStatusString( renderFramebufferStatus ) );
					return false;
				}
			}
		}
	}

	return true;
}

void ovrFramebuffer_Destroy( ovrFramebuffer * frameBuffer )
{
    LOAD_GLES2(glDeleteFramebuffers);
    LOAD_GLES2(glDeleteRenderbuffers);

	GL( gles_glDeleteFramebuffers( frameBuffer->TextureSwapChainLength, frameBuffer->FrameBuffers ) );
	GL( gles_glDeleteRenderbuffers( frameBuffer->TextureSwapChainLength, frameBuffer->DepthBuffers ) );

	vrapi_DestroyTextureSwapChain( frameBuffer->ColorTextureSwapChain );

	free( frameBuffer->DepthBuffers );
	free( frameBuffer->FrameBuffers );

	ovrFramebuffer_Clear( frameBuffer );
}

void GPUWaitSync()
{
	GLsync syncBuff = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
	GLenum status = glClientWaitSync(syncBuff, GL_SYNC_FLUSH_COMMANDS_BIT, 1000 * 1000 * 50); // Wait for a max of 50ms...
	if (status != GL_CONDITION_SATISFIED)
	{
		LOGE("Error on glClientWaitSync: %d\n", status);
	}
	glDeleteSync(syncBuff);
}

void ovrFramebuffer_SetCurrent( ovrFramebuffer * frameBuffer )
{
    LOAD_GLES2(glBindFramebuffer);
	GL( gles_glBindFramebuffer( GL_DRAW_FRAMEBUFFER, frameBuffer->FrameBuffers[frameBuffer->ProcessingTextureSwapChainIndex] ) );
}

void ovrFramebuffer_SetNone()
{
    LOAD_GLES2(glBindFramebuffer);
	GL( gles_glBindFramebuffer( GL_DRAW_FRAMEBUFFER, 0 ) );
}

void ovrFramebuffer_Resolve( ovrFramebuffer * frameBuffer )
{
	// Discard the depth buffer, so the tiler won't need to write it back out to memory.
//	const GLenum depthAttachment[1] = { GL_DEPTH_ATTACHMENT };
//	glInvalidateFramebuffer( GL_DRAW_FRAMEBUFFER, 1, depthAttachment );

    // Flush this frame worth of commands.
    glFlush();
}

void ovrFramebuffer_Advance( ovrFramebuffer * frameBuffer )
{
	// Advance to the next texture from the set.
	frameBuffer->ReadyTextureSwapChainIndex = frameBuffer->ProcessingTextureSwapChainIndex;
	frameBuffer->ProcessingTextureSwapChainIndex = ( frameBuffer->ProcessingTextureSwapChainIndex + 1 ) % frameBuffer->TextureSwapChainLength;
}


void ovrFramebuffer_ClearEdgeTexels( ovrFramebuffer * frameBuffer )
{
	LOAD_GLES2(glEnable);
	LOAD_GLES2(glDisable);
	LOAD_GLES2(glViewport);
	LOAD_GLES2(glScissor);
	LOAD_GLES2(glClearColor);
	LOAD_GLES2(glClear);

	GL( gles_glEnable( GL_SCISSOR_TEST ) );
	GL( gles_glViewport( 0, 0, frameBuffer->Width, frameBuffer->Height ) );

	// Explicitly clear the border texels to black because OpenGL-ES does not support GL_CLAMP_TO_BORDER.
	// Clear to fully opaque black.
	GL( gles_glClearColor( 0.0f, 0.0f, 0.0f, 1.0f ) );

	// bottom
	GL( gles_glScissor( 0, 0, frameBuffer->Width, 1 ) );
	GL( gles_glClear( GL_COLOR_BUFFER_BIT ) );
	// top
	GL( gles_glScissor( 0, frameBuffer->Height - 1, frameBuffer->Width, 1 ) );
	GL( gles_glClear( GL_COLOR_BUFFER_BIT ) );
	// left
	GL( gles_glScissor( 0, 0, 1, frameBuffer->Height ) );
	GL( gles_glClear( GL_COLOR_BUFFER_BIT ) );
	// right
	GL( gles_glScissor( frameBuffer->Width - 1, 0, 1, frameBuffer->Height ) );
	GL( gles_glClear( GL_COLOR_BUFFER_BIT ) );


	GL( gles_glScissor( 0, 0, 0, 0 ) );
	GL( gles_glDisable( GL_SCISSOR_TEST ) );
}


/*
================================================================================

ovrRenderer

================================================================================
*/


void ovrRenderer_Clear( ovrRenderer * renderer )
{
	for ( int eye = 0; eye < VRAPI_FRAME_LAYER_EYE_MAX; eye++ )
	{
		ovrFramebuffer_Clear( &renderer->FrameBuffer[eye] );
	}
	renderer->ProjectionMatrix = ovrMatrix4f_CreateIdentity();
	renderer->NumBuffers = VRAPI_FRAME_LAYER_EYE_MAX;
}


void ovrRenderer_Create( int width, int height, ovrRenderer * renderer, const ovrJava * java )
{
	renderer->NumBuffers = VRAPI_FRAME_LAYER_EYE_MAX;

	//Now using a symmetrical render target, based on the horizontal FOV
    vr.fov = vrapi_GetSystemPropertyInt( java, VRAPI_SYS_PROP_SUGGESTED_EYE_FOV_DEGREES_X);

	// Create the render Textures.
	for ( int eye = 0; eye < VRAPI_FRAME_LAYER_EYE_MAX; eye++ )
	{
		ovrFramebuffer_Create( &renderer->FrameBuffer[eye],
							   GL_RGBA8,
							   width,
							   height,
							   NUM_MULTI_SAMPLES );
	}

	// Setup the projection matrix.
	renderer->ProjectionMatrix = ovrMatrix4f_CreateProjectionFov(
			vr.fov, vr.fov, 0.0f, 0.0f, 1.0f, 0.0f );

}

void ovrRenderer_Destroy( ovrRenderer * renderer )
{
	for ( int eye = 0; eye < renderer->NumBuffers; eye++ )
	{
		ovrFramebuffer_Destroy( &renderer->FrameBuffer[eye] );
	}
	renderer->ProjectionMatrix = ovrMatrix4f_CreateIdentity();
}


#ifndef EPSILON
#define EPSILON 0.001f
#endif

static ovrVector3f normalizeVec(ovrVector3f vec) {
    //NOTE: leave w-component untouched
    //@@const float EPSILON = 0.000001f;
    float xxyyzz = vec.x*vec.x + vec.y*vec.y + vec.z*vec.z;
    //@@if(xxyyzz < EPSILON)
    //@@    return *this; // do nothing if it is zero vector

    //float invLength = invSqrt(xxyyzz);
    ovrVector3f result;
    float invLength = 1.0f / sqrtf(xxyyzz);
    result.x = vec.x * invLength;
    result.y = vec.y * invLength;
    result.z = vec.z * invLength;
    return result;
}

void NormalizeAngles(vec3_t angles)
{
	while (angles[0] >= 90) angles[0] -= 180;
	while (angles[1] >= 180) angles[1] -= 360;
	while (angles[2] >= 180) angles[2] -= 360;
	while (angles[0] < -90) angles[0] += 180;
	while (angles[1] < -180) angles[1] += 360;
	while (angles[2] < -180) angles[2] += 360;
}

void GetAnglesFromVectors(const ovrVector3f forward, const ovrVector3f right, const ovrVector3f up, vec3_t angles)
{
	float sr, sp, sy, cr, cp, cy;

	sp = -forward.z;

	float cp_x_cy = forward.x;
	float cp_x_sy = forward.y;
	float cp_x_sr = -right.z;
	float cp_x_cr = up.z;

	float yaw = atan2(cp_x_sy, cp_x_cy);
	float roll = atan2(cp_x_sr, cp_x_cr);

	cy = cos(yaw);
	sy = sin(yaw);
	cr = cos(roll);
	sr = sin(roll);

	if (fabs(cy) > EPSILON)
	{
	cp = cp_x_cy / cy;
	}
	else if (fabs(sy) > EPSILON)
	{
	cp = cp_x_sy / sy;
	}
	else if (fabs(sr) > EPSILON)
	{
	cp = cp_x_sr / sr;
	}
	else if (fabs(cr) > EPSILON)
	{
	cp = cp_x_cr / cr;
	}
	else
	{
	cp = cos(asin(sp));
	}

	float pitch = atan2(sp, cp);

	angles[0] = pitch / (M_PI*2.f / 360.f);
	angles[1] = yaw / (M_PI*2.f / 360.f);
	angles[2] = roll / (M_PI*2.f / 360.f);

	NormalizeAngles(angles);
}


void QuatToYawPitchRoll(ovrQuatf q, vec3_t rotation, vec3_t out) {

    ovrMatrix4f mat = ovrMatrix4f_CreateFromQuaternion( &q );

    if (rotation[0] != 0.0f || rotation[1] != 0.0f || rotation[2] != 0.0f)
    {
        ovrMatrix4f rot = ovrMatrix4f_CreateRotation(radians(rotation[0]), radians(rotation[1]), radians(rotation[2]));
        mat = ovrMatrix4f_Multiply(&mat, &rot);
    }

    ovrVector4f v1 = {0, 0, -1, 0};
    ovrVector4f v2 = {1, 0, 0, 0};
    ovrVector4f v3 = {0, 1, 0, 0};

    ovrVector4f forwardInVRSpace = ovrVector4f_MultiplyMatrix4f(&mat, &v1);
    ovrVector4f rightInVRSpace = ovrVector4f_MultiplyMatrix4f(&mat, &v2);
    ovrVector4f upInVRSpace = ovrVector4f_MultiplyMatrix4f(&mat, &v3);

    ovrVector3f forward = {-forwardInVRSpace.z, -forwardInVRSpace.x, forwardInVRSpace.y};
    ovrVector3f right = {-rightInVRSpace.z, -rightInVRSpace.x, rightInVRSpace.y};
    ovrVector3f up = {-upInVRSpace.z, -upInVRSpace.x, upInVRSpace.y};

    ovrVector3f forwardNormal = normalizeVec(forward);
    ovrVector3f rightNormal = normalizeVec(right);
    ovrVector3f upNormal = normalizeVec(up);

    GetAnglesFromVectors(forwardNormal, rightNormal, upNormal, out);
    return;
}

void updateHMDOrientation()
{
	//Position
    VectorSubtract(vr.hmdposition_last, vr.hmdposition, vr.hmdposition_delta);

    //Keep this for our records
    VectorCopy(vr.hmdposition, vr.hmdposition_last);

	//Orientation
	VectorSubtract(vr.hmdorientation_last, vr.hmdorientation, vr.hmdorientation_delta);

	//Keep this for our records
	VectorCopy(vr.hmdorientation, vr.hmdorientation_last);
}

void setHMDPosition( float x, float y, float z )
{
	static qboolean s_useScreen = qfalse;

	VectorSet(vr.hmdposition, x, y, z);

    if (s_useScreen != RTCWVR_useScreenLayer())
    {
		s_useScreen = RTCWVR_useScreenLayer();

		//Record player height on transition
        playerHeight = y;

        //Resync yaw on transition
        RTCWVR_ResyncClientYawWithGameYaw();
    }

	if (!RTCWVR_useScreenLayer())
    {
    	playerYaw = vr.hmdorientation[YAW];
	}
}

qboolean isMultiplayer()
{
	return Cvar_VariableValue("maxclients") > 1;
}


/*
========================
RTCWVR_Vibrate
========================
*/

//0 = left, 1 = right
float vibration_channel_duration[2] = {0.0f, 0.0f};
float vibration_channel_intensity[2] = {0.0f, 0.0f};

void RTCWVR_Vibrate( int duration, int channel, float intensity )
{
	if (vibration_channel_duration[channel] > 0.0f)
		return;

	if (vibration_channel_duration[channel] == -1.0f &&	duration != 0.0f)
		return;

	vibration_channel_duration[channel] = duration;
	vibration_channel_intensity[channel] = intensity;
}

void RTCWVR_GetMove(float *forward, float *side, float *pos_forward, float *pos_side, float *up,
                    float *yaw, float *pitch, float *roll)
{
    *forward = remote_movementForward;
    *pos_forward = positional_movementForward;
    *up = remote_movementUp;
    *side = remote_movementSideways;
    *pos_side = positional_movementSideways;
	*yaw = vr.hmdorientation[YAW] + snapTurn;
	*pitch = vr.hmdorientation[PITCH];
	*roll = vr.hmdorientation[ROLL];
}

/*
================================================================================

ovrRenderThread

================================================================================
*/


/*
================================================================================

ovrApp

================================================================================
*/

typedef struct
{
	ovrJava				Java;
	ovrEgl				Egl;
	ANativeWindow *		NativeWindow;
	bool				Resumed;
	ovrMobile *			Ovr;
    ovrScene			Scene;
	long long			FrameIndex;
	double 				DisplayTime;
	int					SwapInterval;
	int					CpuLevel;
	int					GpuLevel;
	int					MainThreadTid;
	int					RenderThreadTid;
	ovrLayer_Union2		Layers[ovrMaxLayerCount];
	int					LayerCount;
	ovrRenderer			Renderer;
} ovrApp;

static void ovrApp_Clear( ovrApp * app )
{
	app->Java.Vm = NULL;
	app->Java.Env = NULL;
	app->Java.ActivityObject = NULL;
	app->Ovr = NULL;
	app->FrameIndex = 1;
	app->DisplayTime = 0;
	app->SwapInterval = 1;
	app->CpuLevel = 3;
	app->GpuLevel = 3;
	app->MainThreadTid = 0;
	app->RenderThreadTid = 0;

	ovrEgl_Clear( &app->Egl );

	ovrScene_Clear( &app->Scene );
	ovrRenderer_Clear( &app->Renderer );
}

static void ovrApp_PushBlackFinal( ovrApp * app )
{
	int frameFlags = 0;
	frameFlags |= VRAPI_FRAME_FLAG_FLUSH | VRAPI_FRAME_FLAG_FINAL;

	ovrLayerProjection2 layer = vrapi_DefaultLayerBlackProjection2();
	layer.Header.Flags |= VRAPI_FRAME_LAYER_FLAG_INHIBIT_SRGB_FRAMEBUFFER;

	const ovrLayerHeader2 * layers[] =
	{
		&layer.Header
	};

	ovrSubmitFrameDescription2 frameDesc = {};
	frameDesc.Flags = frameFlags;
	frameDesc.SwapInterval = 1;
	frameDesc.FrameIndex = app->FrameIndex;
	frameDesc.DisplayTime = app->DisplayTime;
	frameDesc.LayerCount = 1;
	frameDesc.Layers = layers;

	vrapi_SubmitFrame2( app->Ovr, &frameDesc );
}

static void ovrApp_HandleVrModeChanges( ovrApp * app )
{
	if ( app->Resumed != false && app->NativeWindow != NULL )
	{
		if ( app->Ovr == NULL )
		{
			ovrModeParms parms = vrapi_DefaultModeParms( &app->Java );
			// Must reset the FLAG_FULLSCREEN window flag when using a SurfaceView
			parms.Flags |= VRAPI_MODE_FLAG_RESET_WINDOW_FULLSCREEN;

			parms.Flags |= VRAPI_MODE_FLAG_NATIVE_WINDOW;
			parms.Display = (size_t)app->Egl.Display;
			parms.WindowSurface = (size_t)app->NativeWindow;
			parms.ShareContext = (size_t)app->Egl.Context;

			ALOGV( "        eglGetCurrentSurface( EGL_DRAW ) = %p", eglGetCurrentSurface( EGL_DRAW ) );

			ALOGV( "        vrapi_EnterVrMode()" );

			app->Ovr = vrapi_EnterVrMode( &parms );

			ALOGV( "        eglGetCurrentSurface( EGL_DRAW ) = %p", eglGetCurrentSurface( EGL_DRAW ) );

			// If entering VR mode failed then the ANativeWindow was not valid.
			if ( app->Ovr == NULL )
			{
				ALOGE( "Invalid ANativeWindow!" );
				app->NativeWindow = NULL;
			}

			// Set performance parameters once we have entered VR mode and have a valid ovrMobile.
			if ( app->Ovr != NULL )
			{
				vrapi_SetClockLevels( app->Ovr, app->CpuLevel, app->GpuLevel );

				ALOGV( "		vrapi_SetClockLevels( %d, %d )", app->CpuLevel, app->GpuLevel );

				vrapi_SetPerfThread( app->Ovr, VRAPI_PERF_THREAD_TYPE_MAIN, app->MainThreadTid );

				ALOGV( "		vrapi_SetPerfThread( MAIN, %d )", app->MainThreadTid );

				vrapi_SetPerfThread( app->Ovr, VRAPI_PERF_THREAD_TYPE_RENDERER, app->RenderThreadTid );

				ALOGV( "		vrapi_SetPerfThread( RENDERER, %d )", app->RenderThreadTid );
			}
		}
	}
	else
	{
		if ( app->Ovr != NULL )
		{
			ALOGV( "        eglGetCurrentSurface( EGL_DRAW ) = %p", eglGetCurrentSurface( EGL_DRAW ) );

			ALOGV( "        vrapi_LeaveVrMode()" );

			vrapi_LeaveVrMode( app->Ovr );
			app->Ovr = NULL;

			ALOGV( "        eglGetCurrentSurface( EGL_DRAW ) = %p", eglGetCurrentSurface( EGL_DRAW ) );
		}
	}
}


/*
================================================================================

ovrMessageQueue

================================================================================
*/

typedef enum
{
	MQ_WAIT_NONE,		// don't wait
	MQ_WAIT_RECEIVED,	// wait until the consumer thread has received the message
	MQ_WAIT_PROCESSED	// wait until the consumer thread has processed the message
} ovrMQWait;

#define MAX_MESSAGE_PARMS	8
#define MAX_MESSAGES		1024

typedef struct
{
	int			Id;
	ovrMQWait	Wait;
	long long	Parms[MAX_MESSAGE_PARMS];
} ovrMessage;

static void ovrMessage_Init( ovrMessage * message, const int id, const int wait )
{
	message->Id = id;
	message->Wait = wait;
	memset( message->Parms, 0, sizeof( message->Parms ) );
}

static void		ovrMessage_SetPointerParm( ovrMessage * message, int index, void * ptr ) { *(void **)&message->Parms[index] = ptr; }
static void *	ovrMessage_GetPointerParm( ovrMessage * message, int index ) { return *(void **)&message->Parms[index]; }
static void		ovrMessage_SetIntegerParm( ovrMessage * message, int index, int value ) { message->Parms[index] = value; }
static int		ovrMessage_GetIntegerParm( ovrMessage * message, int index ) { return (int)message->Parms[index]; }
static void		ovrMessage_SetFloatParm( ovrMessage * message, int index, float value ) { *(float *)&message->Parms[index] = value; }
static float	ovrMessage_GetFloatParm( ovrMessage * message, int index ) { return *(float *)&message->Parms[index]; }

// Cyclic queue with messages.
typedef struct
{
	ovrMessage	 		Messages[MAX_MESSAGES];
	volatile int		Head;	// dequeue at the head
	volatile int		Tail;	// enqueue at the tail
	ovrMQWait			Wait;
	volatile bool		EnabledFlag;
	volatile bool		PostedFlag;
	volatile bool		ReceivedFlag;
	volatile bool		ProcessedFlag;
	pthread_mutex_t		Mutex;
	pthread_cond_t		PostedCondition;
	pthread_cond_t		ReceivedCondition;
	pthread_cond_t		ProcessedCondition;
} ovrMessageQueue;

static void ovrMessageQueue_Create( ovrMessageQueue * messageQueue )
{
	messageQueue->Head = 0;
	messageQueue->Tail = 0;
	messageQueue->Wait = MQ_WAIT_NONE;
	messageQueue->EnabledFlag = false;
	messageQueue->PostedFlag = false;
	messageQueue->ReceivedFlag = false;
	messageQueue->ProcessedFlag = false;

	pthread_mutexattr_t	attr;
	pthread_mutexattr_init( &attr );
	pthread_mutexattr_settype( &attr, PTHREAD_MUTEX_ERRORCHECK );
	pthread_mutex_init( &messageQueue->Mutex, &attr );
	pthread_mutexattr_destroy( &attr );
	pthread_cond_init( &messageQueue->PostedCondition, NULL );
	pthread_cond_init( &messageQueue->ReceivedCondition, NULL );
	pthread_cond_init( &messageQueue->ProcessedCondition, NULL );
}

static void ovrMessageQueue_Destroy( ovrMessageQueue * messageQueue )
{
	pthread_mutex_destroy( &messageQueue->Mutex );
	pthread_cond_destroy( &messageQueue->PostedCondition );
	pthread_cond_destroy( &messageQueue->ReceivedCondition );
	pthread_cond_destroy( &messageQueue->ProcessedCondition );
}

static void ovrMessageQueue_Enable( ovrMessageQueue * messageQueue, const bool set )
{
	messageQueue->EnabledFlag = set;
}

static void ovrMessageQueue_PostMessage( ovrMessageQueue * messageQueue, const ovrMessage * message )
{
	if ( !messageQueue->EnabledFlag )
	{
		return;
	}
	while ( messageQueue->Tail - messageQueue->Head >= MAX_MESSAGES )
	{
		usleep( 1000 );
	}
	pthread_mutex_lock( &messageQueue->Mutex );
	messageQueue->Messages[messageQueue->Tail & ( MAX_MESSAGES - 1 )] = *message;
	messageQueue->Tail++;
	messageQueue->PostedFlag = true;
	pthread_cond_broadcast( &messageQueue->PostedCondition );
	if ( message->Wait == MQ_WAIT_RECEIVED )
	{
		while ( !messageQueue->ReceivedFlag )
		{
			pthread_cond_wait( &messageQueue->ReceivedCondition, &messageQueue->Mutex );
		}
		messageQueue->ReceivedFlag = false;
	}
	else if ( message->Wait == MQ_WAIT_PROCESSED )
	{
		while ( !messageQueue->ProcessedFlag )
		{
			pthread_cond_wait( &messageQueue->ProcessedCondition, &messageQueue->Mutex );
		}
		messageQueue->ProcessedFlag = false;
	}
	pthread_mutex_unlock( &messageQueue->Mutex );
}

static void ovrMessageQueue_SleepUntilMessage( ovrMessageQueue * messageQueue )
{
	if ( messageQueue->Wait == MQ_WAIT_PROCESSED )
	{
		messageQueue->ProcessedFlag = true;
		pthread_cond_broadcast( &messageQueue->ProcessedCondition );
		messageQueue->Wait = MQ_WAIT_NONE;
	}
	pthread_mutex_lock( &messageQueue->Mutex );
	if ( messageQueue->Tail > messageQueue->Head )
	{
		pthread_mutex_unlock( &messageQueue->Mutex );
		return;
	}
	while ( !messageQueue->PostedFlag )
	{
		pthread_cond_wait( &messageQueue->PostedCondition, &messageQueue->Mutex );
	}
	messageQueue->PostedFlag = false;
	pthread_mutex_unlock( &messageQueue->Mutex );
}

static bool ovrMessageQueue_GetNextMessage( ovrMessageQueue * messageQueue, ovrMessage * message, bool waitForMessages )
{
	if ( messageQueue->Wait == MQ_WAIT_PROCESSED )
	{
		messageQueue->ProcessedFlag = true;
		pthread_cond_broadcast( &messageQueue->ProcessedCondition );
		messageQueue->Wait = MQ_WAIT_NONE;
	}
	if ( waitForMessages )
	{
		ovrMessageQueue_SleepUntilMessage( messageQueue );
	}
	pthread_mutex_lock( &messageQueue->Mutex );
	if ( messageQueue->Tail <= messageQueue->Head )
	{
		pthread_mutex_unlock( &messageQueue->Mutex );
		return false;
	}
	*message = messageQueue->Messages[messageQueue->Head & ( MAX_MESSAGES - 1 )];
	messageQueue->Head++;
	pthread_mutex_unlock( &messageQueue->Mutex );
	if ( message->Wait == MQ_WAIT_RECEIVED )
	{
		messageQueue->ReceivedFlag = true;
		pthread_cond_broadcast( &messageQueue->ReceivedCondition );
	}
	else if ( message->Wait == MQ_WAIT_PROCESSED )
	{
		messageQueue->Wait = MQ_WAIT_PROCESSED;
	}
	return true;
}

/*
================================================================================

ovrAppThread

================================================================================
*/

enum
{
	MESSAGE_ON_CREATE,
	MESSAGE_ON_START,
	MESSAGE_ON_RESUME,
	MESSAGE_ON_PAUSE,
	MESSAGE_ON_STOP,
	MESSAGE_ON_DESTROY,
	MESSAGE_ON_SURFACE_CREATED,
	MESSAGE_ON_SURFACE_DESTROYED
};

typedef struct
{
	JavaVM *		JavaVm;
	jobject			ActivityObject;
	jclass          ActivityClass;
	pthread_t		Thread;
	ovrMessageQueue	MessageQueue;
	ANativeWindow * NativeWindow;
} ovrAppThread;

long shutdownCountdown;

int m_width;
int m_height;

qboolean R_SetMode( void );

void RTCWVR_GetScreenRes(int *width, int *height)
{
    *width = m_width;
    *height = m_height;
}

void Android_MessageBox(const char *title, const char *text)
{
    ALOGE("%s %s", title, text);
}

void initialize_gl4es();

void RTCWVR_ResyncClientYawWithGameYaw()
{
	//Allow several frames for the yaw to sync, first is this frame which is the old yaw
	//second is the next frame which _should_ be the new yaw, but just in case it isn't
	//we resync on the 3rd frame as well
	resyncClientYawWithGameYaw = 2;
}

void RTCWVR_Init()
{
	//Initialise all our variables
	playerYaw = 0.0f;
	showingScreenLayer = qfalse;
	remote_movementSideways = 0.0f;
	remote_movementForward = 0.0f;
	remote_movementUp = 0.0f;
	positional_movementSideways = 0.0f;
	positional_movementForward = 0.0f;
	snapTurn = 0.0f;
	ducked = DUCK_NOTDUCKED;
	RTCWVR_ResyncClientYawWithGameYaw();

	//init randomiser
	srand(time(NULL));

	//Create Cvars
	vr_turn_mode = Cvar_Get( "vr_turn_mode", "0", CVAR_ARCHIVE); // 0 = snap, 1 = smooth
	vr_turn_angle = Cvar_Get( "vr_turn_angle", "45", CVAR_ARCHIVE);
	vr_reloadtimeoutms = Cvar_Get( "vr_reloadtimeoutms", "200", CVAR_ARCHIVE);
	vr_positional_factor = Cvar_Get( "vr_positional_factor", "12", CVAR_ARCHIVE);
    vr_walkdirection = Cvar_Get( "vr_walkdirection", "0", CVAR_ARCHIVE);
	vr_movement_multiplier = Cvar_Get( "vr_movement_multiplier", "0.7", CVAR_ARCHIVE);
	vr_weapon_pitchadjust = Cvar_Get( "vr_weapon_pitchadjust", "-20.0", CVAR_ARCHIVE);
	vr_lasersight = Cvar_Get( "vr_lasersight", "0", CVAR_LATCH);
	vr_teleport = Cvar_Get( "vr_teleport", "0", CVAR_ARCHIVE);

    //Defaults
	vr_control_scheme = Cvar_Get( "vr_control_scheme", "0", CVAR_ARCHIVE);
	vr_switch_sticks = Cvar_Get( "vr_switch_sticks", "0", CVAR_ARCHIVE);

    //Set up vr client info
	vr.backpackitemactive = 0;
	vr.visible_hud = qtrue;
	vr.dualwield = qfalse;

	//Clear teleport stuff
	vr.teleportexecute = qfalse;
	vr.teleportseek = qfalse;
	vr.teleportenabled = qfalse;
	vr.teleportready = qfalse;
}


static ovrAppThread * gAppThread = NULL;
static ovrApp gAppState;
static ovrJava java;
static qboolean destroyed = qfalse;

void RTCWVR_prepareEyeBuffer(int eye )
{
	ovrRenderer *renderer = RTCWVR_useScreenLayer() ? &gAppState.Scene.CylinderRenderer : &gAppState.Renderer;

	ovrFramebuffer *frameBuffer = &(renderer->FrameBuffer[eye]);
	ovrFramebuffer_SetCurrent(frameBuffer);

	GL(glEnable(GL_SCISSOR_TEST));
	GL(glDepthMask(GL_TRUE));
	GL(glEnable(GL_DEPTH_TEST));
	GL(glDepthFunc(GL_LEQUAL));

	//Weusing the size of the render target
	GL(glViewport(0, 0, frameBuffer->Width, frameBuffer->Height));
	GL(glScissor(0, 0, frameBuffer->Width, frameBuffer->Height));

	GL(glClearColor(0.0f, 0.0f, 0.0f, 1.0f));
	GL(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));
	GL(glDisable(GL_SCISSOR_TEST));
}

void RTCWVR_finishEyeBuffer(int eye )
{
	ovrRenderer *renderer = RTCWVR_useScreenLayer() ? &gAppState.Scene.CylinderRenderer : &gAppState.Renderer;

	ovrFramebuffer *frameBuffer = &(renderer->FrameBuffer[eye]);

	//Clear edge to prevent smearing
	ovrFramebuffer_ClearEdgeTexels(frameBuffer);
	ovrFramebuffer_Resolve(frameBuffer);
	ovrFramebuffer_Advance(frameBuffer);

	ovrFramebuffer_SetNone();
}

qboolean RTCWVR_processMessageQueue() {
	for ( ; ; )
	{
		ovrMessage message;
		const bool waitForMessages = ( gAppState.Ovr == NULL && destroyed == false );
		if ( !ovrMessageQueue_GetNextMessage( &gAppThread->MessageQueue, &message, waitForMessages ) )
		{
			break;
		}

		switch ( message.Id )
		{
			case MESSAGE_ON_CREATE:
			{
				break;
			}
			case MESSAGE_ON_START:
			{
				if (!rtcw_initialised)
				{
					ALOGV( "    Initialising rtcw Engine" );

					//Set command line arguments here
					if (argc != 0)
					{
						//TODO
					}
					else
					{
						int argc = 1; char *argv[] = { "rtcw" };

					}

					rtcw_initialised = qtrue;
				}
				break;
			}
			case MESSAGE_ON_RESUME:
			{
				//If we get here, then user has opted not to quit
				gAppState.Resumed = true;
				break;
			}
			case MESSAGE_ON_PAUSE:
			{
				gAppState.Resumed = false;
				break;
			}
			case MESSAGE_ON_STOP:
			{
				break;
			}
			case MESSAGE_ON_DESTROY:
			{
				gAppState.NativeWindow = NULL;
				destroyed = true;
				//shutdown = true;
				break;
			}
			case MESSAGE_ON_SURFACE_CREATED:	{ gAppState.NativeWindow = (ANativeWindow *)ovrMessage_GetPointerParm( &message, 0 ); break; }
			case MESSAGE_ON_SURFACE_DESTROYED:	{ gAppState.NativeWindow = NULL; break; }
		}

		ovrApp_HandleVrModeChanges( &gAppState );
	}
}

void showLoadingIcon();
void jni_shutdown();
void RTCWVR_incrementFrameIndex();
void shutdownVR();
int VR_main( int argc, char* argv[] );

void * AppThreadFunction(void * parm ) {
	gAppThread = (ovrAppThread *) parm;

	java.Vm = gAppThread->JavaVm;
	(*java.Vm)->AttachCurrentThread(java.Vm, &java.Env, NULL);
	java.ActivityObject = gAppThread->ActivityObject;

	jclass cls = (*java.Env)->GetObjectClass(java.Env, java.ActivityObject);

	// Note that AttachCurrentThread will reset the thread name.
	prctl(PR_SET_NAME, (long) "OVR::Main", 0, 0, 0);

	rtcw_initialised = false;

	const ovrInitParms initParms = vrapi_DefaultInitParms(&java);
	int32_t initResult = vrapi_Initialize(&initParms);
	if (initResult != VRAPI_INITIALIZE_SUCCESS) {
		// If intialization failed, vrapi_* function calls will not be available.
		exit(0);
	}

	ovrApp_Clear(&gAppState);
	gAppState.Java = java;

	// This app will handle android gamepad events itself.
	vrapi_SetPropertyInt(&gAppState.Java, VRAPI_EAT_NATIVE_GAMEPAD_EVENTS, 0);

	//Using a symmetrical render target
	m_height = m_width = (int)(vrapi_GetSystemPropertyInt(&java, VRAPI_SYS_PROP_SUGGESTED_EYE_TEXTURE_WIDTH) *  SS_MULTIPLIER);

	gAppState.CpuLevel = CPU_LEVEL;
	gAppState.GpuLevel = GPU_LEVEL;
	gAppState.MainThreadTid = gettid();

	ovrEgl_CreateContext(&gAppState.Egl, NULL);

	EglInitExtensions();

	//First handle any messages in the queue
	while ( gAppState.Ovr == NULL ) {
		RTCWVR_processMessageQueue();
	}

	ovrRenderer_Create(m_width, m_height, &gAppState.Renderer, &java);

	if ( gAppState.Ovr == NULL )
	{
		return NULL;
	}

	// Create the scene if not yet created.
	ovrScene_Create( m_width, m_height, &gAppState.Scene, &java );

	chdir("/sdcard/RTCWQuest");

	//Run loading loop until we are ready to start RTCWVR
	while (!destroyed && !rtcw_initialised) {
		RTCWVR_processMessageQueue();
		RTCWVR_incrementFrameIndex();
		showLoadingIcon();
	}

	//start
	VR_main(argc, argv);

	//We are done, shutdown cleanly
	shutdownVR();

	//Ask Java to shut down
	jni_shutdown();

	return NULL;
}

//All the stuff we want to do each frame
void RTCWVR_FrameSetup()
{
	//Use floor based tracking space
	vrapi_SetTrackingSpace(gAppState.Ovr, VRAPI_TRACKING_SPACE_LOCAL_FLOOR);
}

void RTCWVR_processHaptics() {
	static float lastFrameTime = 0.0f;
	float timestamp = (float)(GetTimeInMilliSeconds());
	float frametime = timestamp - lastFrameTime;
	lastFrameTime = timestamp;

	for (int i = 0; i < 2; ++i) {
		if (vibration_channel_duration[i] > 0.0f ||
			vibration_channel_duration[i] == -1.0f) {
			vrapi_SetHapticVibrationSimple(gAppState.Ovr, controllerIDs[i],
										   vibration_channel_intensity[i]);

			if (vibration_channel_duration[i] != -1.0f) {
				vibration_channel_duration[i] -= frametime;

				if (vibration_channel_duration[i] < 0.0f) {
					vibration_channel_duration[i] = 0.0f;
					vibration_channel_intensity[i] = 0.0f;
				}
			}
		} else {
			vrapi_SetHapticVibrationSimple(gAppState.Ovr, controllerIDs[i], 0.0f);
		}
	}
}

void showLoadingIcon()
{
	int frameFlags = 0;
	frameFlags |= VRAPI_FRAME_FLAG_FLUSH;

	ovrLayerProjection2 blackLayer = vrapi_DefaultLayerBlackProjection2();
	blackLayer.Header.Flags |= VRAPI_FRAME_LAYER_FLAG_INHIBIT_SRGB_FRAMEBUFFER;

	ovrLayerLoadingIcon2 iconLayer = vrapi_DefaultLayerLoadingIcon2();
	iconLayer.Header.Flags |= VRAPI_FRAME_LAYER_FLAG_INHIBIT_SRGB_FRAMEBUFFER;

	const ovrLayerHeader2 * layers[] =
			{
					&blackLayer.Header,
					&iconLayer.Header,
			};

	ovrSubmitFrameDescription2 frameDesc = {};
	frameDesc.Flags = frameFlags;
	frameDesc.SwapInterval = 1;
	frameDesc.FrameIndex = gAppState.FrameIndex;
	frameDesc.DisplayTime = gAppState.DisplayTime;
	frameDesc.LayerCount = 2;
	frameDesc.Layers = layers;

	vrapi_SubmitFrame2( gAppState.Ovr, &frameDesc );
}

void RTCWVR_getHMDOrientation() {//Get orientation

	// Get the HMD pose, predicted for the middle of the time period during which
	// the new eye images will be displayed. The number of frames predicted ahead
	// depends on the pipeline depth of the engine and the synthesis rate.
	// The better the prediction, the less black will be pulled in at the edges.
	tracking = vrapi_GetPredictedTracking2(gAppState.Ovr, gAppState.DisplayTime);


	// We extract Yaw, Pitch, Roll instead of directly using the orientation
	// to allow "additional" yaw manipulation with mouse/controller.
	const ovrQuatf quatHmd = tracking.HeadPose.Pose.Orientation;
	const ovrVector3f positionHmd = tracking.HeadPose.Pose.Position;
	vec3_t rotation = {0};
	QuatToYawPitchRoll(quatHmd, rotation, vr.hmdorientation);
	setHMDPosition(positionHmd.x, positionHmd.y, positionHmd.z);

	updateHMDOrientation();

	ALOGV("        HMD-Position: %f, %f, %f", positionHmd.x, positionHmd.y, positionHmd.z);
}

void shutdownVR() {
	ovrRenderer_Destroy( &gAppState.Renderer );
	ovrEgl_DestroyContext( &gAppState.Egl );
	(*java.Vm)->DetachCurrentThread( java.Vm );
	vrapi_Shutdown();
}

long long RTCWVR_getFrameIndex()
{
	return gAppState.FrameIndex;
}

void RTCWVR_incrementFrameIndex()
{
	// This is the only place the frame index is incremented, right before
	// calling vrapi_GetPredictedDisplayTime().
	gAppState.FrameIndex++;
	gAppState.DisplayTime = vrapi_GetPredictedDisplayTime(gAppState.Ovr,
														  gAppState.FrameIndex);
}

void RTCWVR_getTrackedRemotesOrientation() {//Get info for tracked remotes
	acquireTrackedRemotesData(gAppState.Ovr, gAppState.DisplayTime);

	//Call additional control schemes here
	switch (vr_control_scheme->integer)
	{
		case RIGHT_HANDED_DEFAULT:
			HandleInput_Default(&rightTrackedRemoteState_new, &rightTrackedRemoteState_old, &rightRemoteTracking_new,
								&leftTrackedRemoteState_new, &leftTrackedRemoteState_old, &leftRemoteTracking_new,
								ovrButton_A, ovrButton_B, ovrButton_X, ovrButton_Y);
			break;
		case LEFT_HANDED_DEFAULT:
			HandleInput_Default(&leftTrackedRemoteState_new, &leftTrackedRemoteState_old, &leftRemoteTracking_new,
								&rightTrackedRemoteState_new, &rightTrackedRemoteState_old, &rightRemoteTracking_new,
								ovrButton_X, ovrButton_Y, ovrButton_A, ovrButton_B);
			break;
		case WEAPON_ALIGN:
			HandleInput_WeaponAlign(&rightTrackedRemoteState_new, &rightTrackedRemoteState_old, &rightRemoteTracking_new,
								&leftTrackedRemoteState_new, &leftTrackedRemoteState_old, &leftRemoteTracking_new,
								ovrButton_A, ovrButton_B, ovrButton_X, ovrButton_Y);
			break;
	}
}

void RTCWVR_submitFrame()
{
	ovrSubmitFrameDescription2 frameDesc = {0};

	if (!RTCWVR_useScreenLayer()) {

		ovrLayerProjection2 layer = vrapi_DefaultLayerProjection2();
		layer.HeadPose = tracking.HeadPose;
		for ( int eye = 0; eye < VRAPI_FRAME_LAYER_EYE_MAX; eye++ )
		{
			ovrFramebuffer * frameBuffer = &gAppState.Renderer.FrameBuffer[gAppState.Renderer.NumBuffers == 1 ? 0 : eye];
			layer.Textures[eye].ColorSwapChain = frameBuffer->ColorTextureSwapChain;
			layer.Textures[eye].SwapChainIndex = frameBuffer->ReadyTextureSwapChainIndex;

			ovrMatrix4f projectionMatrix;
			projectionMatrix = ovrMatrix4f_CreateProjectionFov(vr.fov, vr.fov,
															   0.0f, 0.0f, 0.1f, 0.0f);

			layer.Textures[eye].TexCoordsFromTanAngles = ovrMatrix4f_TanAngleMatrixFromProjection(&projectionMatrix);

			layer.Textures[eye].TextureRect.x = 0;
			layer.Textures[eye].TextureRect.y = 0;
			layer.Textures[eye].TextureRect.width = 1.0f;
			layer.Textures[eye].TextureRect.height = 1.0f;
		}
		layer.Header.Flags |= VRAPI_FRAME_LAYER_FLAG_CHROMATIC_ABERRATION_CORRECTION;

		// Set up the description for this frame.
		const ovrLayerHeader2 *layers[] =
				{
						&layer.Header
				};

		frameDesc.Flags = 0;
		frameDesc.SwapInterval = gAppState.SwapInterval;
		frameDesc.FrameIndex = gAppState.FrameIndex;
		frameDesc.DisplayTime = gAppState.DisplayTime;
		frameDesc.LayerCount = 1;
		frameDesc.Layers = layers;

		// Hand over the eye images to the time warp.
		vrapi_SubmitFrame2(gAppState.Ovr, &frameDesc);

	} else {
		// Set-up the compositor layers for this frame.
		// NOTE: Multiple independent layers are allowed, but they need to be added
		// in a depth consistent order.
		memset( gAppState.Layers, 0, sizeof( ovrLayer_Union2 ) * ovrMaxLayerCount );
		gAppState.LayerCount = 0;

		// Add a simple cylindrical layer
		gAppState.Layers[gAppState.LayerCount++].Cylinder =
				BuildCylinderLayer(&gAppState.Scene.CylinderRenderer,
								   gAppState.Scene.CylinderWidth, gAppState.Scene.CylinderHeight, &tracking, radians(playerYaw) );

		// Compose the layers for this frame.
		const ovrLayerHeader2 * layerHeaders[ovrMaxLayerCount] = { 0 };
		for ( int i = 0; i < gAppState.LayerCount; i++ )
		{
			layerHeaders[i] = &gAppState.Layers[i].Header;
		}

		// Set up the description for this frame.
		frameDesc.Flags = 0;
		frameDesc.SwapInterval = gAppState.SwapInterval;
		frameDesc.FrameIndex = gAppState.FrameIndex;
		frameDesc.DisplayTime = gAppState.DisplayTime;
		frameDesc.LayerCount = gAppState.LayerCount;
		frameDesc.Layers = layerHeaders;

		// Hand over the eye images to the time warp.
		vrapi_SubmitFrame2(gAppState.Ovr, &frameDesc);
	}


	RTCWVR_incrementFrameIndex();
}

static void ovrAppThread_Create( ovrAppThread * appThread, JNIEnv * env, jobject activityObject, jclass activityClass )
{
	(*env)->GetJavaVM( env, &appThread->JavaVm );
	appThread->ActivityObject = (*env)->NewGlobalRef( env, activityObject );
	appThread->ActivityClass = (*env)->NewGlobalRef( env, activityClass );
	appThread->Thread = 0;
	appThread->NativeWindow = NULL;
	ovrMessageQueue_Create( &appThread->MessageQueue );

	const int createErr = pthread_create( &appThread->Thread, NULL, AppThreadFunction, appThread );
	if ( createErr != 0 )
	{
		ALOGE( "pthread_create returned %i", createErr );
	}
}

static void ovrAppThread_Destroy( ovrAppThread * appThread, JNIEnv * env )
{
	pthread_join( appThread->Thread, NULL );
	(*env)->DeleteGlobalRef( env, appThread->ActivityObject );
	(*env)->DeleteGlobalRef( env, appThread->ActivityClass );
	ovrMessageQueue_Destroy( &appThread->MessageQueue );
}

/*
================================================================================

Activity lifecycle

================================================================================
*/






/*




jmethodID android_shutdown;
static JavaVM *jVM;
static jobject shutdownCallbackObj=0;

void jni_shutdown()
{
	ALOGV("Calling: jni_shutdown");
	JNIEnv *env;
	jobject tmp;
	if (((*jVM)->GetEnv(jVM, (void**) &env, JNI_VERSION_1_4))<0)
	{
		(*jVM)->AttachCurrentThread(jVM,&env, NULL);
	}
	return (*env)->CallVoidMethod(env, shutdownCallbackObj, android_shutdown);
}

int JNI_OnLoad(JavaVM* vm, void* reserved)
{
	JNIEnv *env;
    jVM = vm;
	if((*vm)->GetEnv(vm, (void**) &env, JNI_VERSION_1_4) != JNI_OK)
	{
		ALOGE("Failed JNI_OnLoad");
		return -1;
	}

	return JNI_VERSION_1_4;
}





JNIEXPORT jlong JNICALL Java_com_drbeef_rtcwquest_GLES3JNILib_onCreate( JNIEnv * env, jclass activityClass, jobject activity,
																	   jstring commandLineParams)
{
	ALOGV( "    GLES3JNILib::onCreate()" );

	// the global arg_xxx structs are initialised within the argtable
	void *argtable[] = {
			ss   = arg_dbl0("s", "supersampling", "<double>", "super sampling value (e.g. 1.0)"),
            cpu   = arg_int0("c", "cpu", "<int>", "CPU perf index 1-4 (default: 2)"),
            gpu   = arg_int0("g", "gpu", "<int>", "GPU perf index 1-4 (default: 3)"),
			end     = arg_end(20)
	};

	jboolean iscopy;
	const char *arg = (*env)->GetStringUTFChars(env, commandLineParams, &iscopy);

	char *cmdLine = NULL;
	if (arg && strlen(arg))
	{
		cmdLine = strdup(arg);
	}

	(*env)->ReleaseStringUTFChars(env, commandLineParams, arg);

	ALOGV("Command line %s", cmdLine);
	argv = malloc(sizeof(char*) * 255);
	argc = ParseCommandLine(strdup(cmdLine), argv);

	// verify the argtable[] entries were allocated sucessfully 
	if (arg_nullcheck(argtable) == 0) {
		// Parse the command line as defined by argtable[]
		arg_parse(argc, argv, argtable);

        if (ss->count > 0 && ss->dval[0] > 0.0)
        {
            SS_MULTIPLIER = ss->dval[0];
        }

        if (cpu->count > 0 && cpu->ival[0] > 0 && cpu->ival[0] < 10)
        {
            CPU_LEVEL = cpu->ival[0];
        }

        if (gpu->count > 0 && gpu->ival[0] > 0 && gpu->ival[0] < 10)
        {
            GPU_LEVEL = gpu->ival[0];
        }
	}
	
	initialize_gl4es();

	ovrAppThread * appThread = (ovrAppThread *) malloc( sizeof( ovrAppThread ) );
	ovrAppThread_Create( appThread, env, activity, activityClass );

	ovrMessageQueue_Enable( &appThread->MessageQueue, true );
	ovrMessage message;
	ovrMessage_Init( &message, MESSAGE_ON_CREATE, MQ_WAIT_PROCESSED );
	ovrMessageQueue_PostMessage( &appThread->MessageQueue, &message );

	return (jlong)((size_t)appThread);
}


JNIEXPORT void JNICALL Java_com_drbeef_rtcwquest_GLES3JNILib_onStart( JNIEnv * env, jobject obj, jlong handle, jobject obj1)
{
	ALOGV( "    GLES3JNILib::onStart()" );


	shutdownCallbackObj = (jobject)(*env)->NewGlobalRef(env, obj1);
	jclass callbackClass = (*env)->GetObjectClass(env, shutdownCallbackObj);

	android_shutdown = (*env)->GetMethodID(env,callbackClass,"shutdown","()V");

	ovrAppThread * appThread = (ovrAppThread *)((size_t)handle);
	ovrMessage message;
	ovrMessage_Init( &message, MESSAGE_ON_START, MQ_WAIT_PROCESSED );
	ovrMessageQueue_PostMessage( &appThread->MessageQueue, &message );
}

JNIEXPORT void JNICALL Java_com_drbeef_rtcwquest_GLES3JNILib_onResume( JNIEnv * env, jobject obj, jlong handle )
{
	ALOGV( "    GLES3JNILib::onResume()" );
	ovrAppThread * appThread = (ovrAppThread *)((size_t)handle);
	ovrMessage message;
	ovrMessage_Init( &message, MESSAGE_ON_RESUME, MQ_WAIT_PROCESSED );
	ovrMessageQueue_PostMessage( &appThread->MessageQueue, &message );
}

JNIEXPORT void JNICALL Java_com_drbeef_rtcwquest_GLES3JNILib_onPause( JNIEnv * env, jobject obj, jlong handle )
{
	ALOGV( "    GLES3JNILib::onPause()" );
	ovrAppThread * appThread = (ovrAppThread *)((size_t)handle);
	ovrMessage message;
	ovrMessage_Init( &message, MESSAGE_ON_PAUSE, MQ_WAIT_PROCESSED );
	ovrMessageQueue_PostMessage( &appThread->MessageQueue, &message );
}

JNIEXPORT void JNICALL Java_com_drbeef_rtcwquest_GLES3JNILib_onStop( JNIEnv * env, jobject obj, jlong handle )
{
	ALOGV( "    GLES3JNILib::onStop()" );
	ovrAppThread * appThread = (ovrAppThread *)((size_t)handle);
	ovrMessage message;
	ovrMessage_Init( &message, MESSAGE_ON_STOP, MQ_WAIT_PROCESSED );
	ovrMessageQueue_PostMessage( &appThread->MessageQueue, &message );
}

JNIEXPORT void JNICALL Java_com_drbeef_rtcwquest_GLES3JNILib_onDestroy( JNIEnv * env, jobject obj, jlong handle )
{
	ALOGV( "    GLES3JNILib::onDestroy()" );
	ovrAppThread * appThread = (ovrAppThread *)((size_t)handle);
	ovrMessage message;
	ovrMessage_Init( &message, MESSAGE_ON_DESTROY, MQ_WAIT_PROCESSED );
	ovrMessageQueue_PostMessage( &appThread->MessageQueue, &message );
	ovrMessageQueue_Enable( &appThread->MessageQueue, false );

	ovrAppThread_Destroy( appThread, env );
	free( appThread );
}



*/


/*
================================================================================

Surface lifecycle

================================================================================
*/





/*




JNIEXPORT void JNICALL Java_com_drbeef_rtcwquest_GLES3JNILib_onSurfaceCreated( JNIEnv * env, jobject obj, jlong handle, jobject surface )
{
	ALOGV( "    GLES3JNILib::onSurfaceCreated()" );
	ovrAppThread * appThread = (ovrAppThread *)((size_t)handle);

	ANativeWindow * newNativeWindow = ANativeWindow_fromSurface( env, surface );
	if ( ANativeWindow_getWidth( newNativeWindow ) < ANativeWindow_getHeight( newNativeWindow ) )
	{
		// An app that is relaunched after pressing the home button gets an initial surface with
		// the wrong orientation even though android:screenOrientation="landscape" is set in the
		// manifest. The choreographer callback will also never be called for this surface because
		// the surface is immediately replaced with a new surface with the correct orientation.
		ALOGE( "        Surface not in landscape mode!" );
	}

	ALOGV( "        NativeWindow = ANativeWindow_fromSurface( env, surface )" );
	appThread->NativeWindow = newNativeWindow;
	ovrMessage message;
	ovrMessage_Init( &message, MESSAGE_ON_SURFACE_CREATED, MQ_WAIT_PROCESSED );
	ovrMessage_SetPointerParm( &message, 0, appThread->NativeWindow );
	ovrMessageQueue_PostMessage( &appThread->MessageQueue, &message );
}

JNIEXPORT void JNICALL Java_com_drbeef_rtcwquest_GLES3JNILib_onSurfaceChanged( JNIEnv * env, jobject obj, jlong handle, jobject surface )
{
	ALOGV( "    GLES3JNILib::onSurfaceChanged()" );
	ovrAppThread * appThread = (ovrAppThread *)((size_t)handle);

	ANativeWindow * newNativeWindow = ANativeWindow_fromSurface( env, surface );
	if ( ANativeWindow_getWidth( newNativeWindow ) < ANativeWindow_getHeight( newNativeWindow ) )
	{
		// An app that is relaunched after pressing the home button gets an initial surface with
		// the wrong orientation even though android:screenOrientation="landscape" is set in the
		// manifest. The choreographer callback will also never be called for this surface because
		// the surface is immediately replaced with a new surface with the correct orientation.
		ALOGE( "        Surface not in landscape mode!" );
	}

	if ( newNativeWindow != appThread->NativeWindow )
	{
		if ( appThread->NativeWindow != NULL )
		{
			ovrMessage message;
			ovrMessage_Init( &message, MESSAGE_ON_SURFACE_DESTROYED, MQ_WAIT_PROCESSED );
			ovrMessageQueue_PostMessage( &appThread->MessageQueue, &message );
			ALOGV( "        ANativeWindow_release( NativeWindow )" );
			ANativeWindow_release( appThread->NativeWindow );
			appThread->NativeWindow = NULL;
		}
		if ( newNativeWindow != NULL )
		{
			ALOGV( "        NativeWindow = ANativeWindow_fromSurface( env, surface )" );
			appThread->NativeWindow = newNativeWindow;
			ovrMessage message;
			ovrMessage_Init( &message, MESSAGE_ON_SURFACE_CREATED, MQ_WAIT_PROCESSED );
			ovrMessage_SetPointerParm( &message, 0, appThread->NativeWindow );
			ovrMessageQueue_PostMessage( &appThread->MessageQueue, &message );
		}
	}
	else if ( newNativeWindow != NULL )
	{
		ANativeWindow_release( newNativeWindow );
	}
}

JNIEXPORT void JNICALL Java_com_drbeef_rtcwquest_GLES3JNILib_onSurfaceDestroyed( JNIEnv * env, jobject obj, jlong handle )
{
	ALOGV( "    GLES3JNILib::onSurfaceDestroyed()" );
	ovrAppThread * appThread = (ovrAppThread *)((size_t)handle);
	ovrMessage message;
	ovrMessage_Init( &message, MESSAGE_ON_SURFACE_DESTROYED, MQ_WAIT_PROCESSED );
	ovrMessageQueue_PostMessage( &appThread->MessageQueue, &message );
	ALOGV( "        ANativeWindow_release( NativeWindow )" );
	ANativeWindow_release( appThread->NativeWindow );
	appThread->NativeWindow = NULL;
}
*/
