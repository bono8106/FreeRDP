/*
   Android JNI Client Layer

   Copyright 2010-2012 Marc-Andre Moreau <marcandre.moreau@gmail.com>
   Copyright 2013 Thinstuff Technologies GmbH, Author: Martin Fleisz

   This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0. 
   If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <locale.h>
#include <sys/select.h>
#include <freerdp/codec/rfx.h>
#include <freerdp/channels/channels.h>
#include <freerdp/gdi/gdi.h>
#include <freerdp/utils/event.h>
#include <freerdp/constants.h>
#include <freerdp/locale/keyboard.h>

#include <android/bitmap.h>
#include <machine/cpu-features.h>

#include "android_freerdp.h"
#include "android_jni_callback.h"
#include "android_debug.h"

struct thread_data
{
	freerdp* instance;
};


void android_context_new(freerdp* instance, rdpContext* context)
{
	context->channels = freerdp_channels_new();
	android_event_queue_init(instance);	
}

void android_context_free(freerdp* instance, rdpContext* context)
{
	freerdp_channels_free(context->channels);
	android_event_queue_uninit(instance);
}


void android_begin_paint(rdpContext* context)
{
	rdpGdi* gdi = context->gdi;
	gdi->primary->hdc->hwnd->invalid->null = 1;
	gdi->primary->hdc->hwnd->ninvalid = 0;
}


void android_end_paint(rdpContext* context)
{
	DEBUG_ANDROID("ui_update");

	rdpGdi *gdi = context->gdi;
	if (gdi->primary->hdc->hwnd->invalid->null)
		return;

	int x = gdi->primary->hdc->hwnd->invalid->x;
	int y = gdi->primary->hdc->hwnd->invalid->y;
	int w = gdi->primary->hdc->hwnd->invalid->w;
	int h = gdi->primary->hdc->hwnd->invalid->h;

	DEBUG_ANDROID("ui_update: x:%d y:%d w:%d h:%d", x, y, w, h);

	freerdp_callback("OnGraphicsUpdate", "(IIIII)V", context->instance, x, y, w, h);
}

void android_desktop_resize(rdpContext* context)
{
	DEBUG_ANDROID("ui_desktop_resize");

	rdpGdi *gdi = context->gdi;
	freerdp_callback("OnGraphicsResize", "(IIII)V", context->instance, gdi->width, gdi->height, gdi->dstBpp);
}


BOOL android_pre_connect(freerdp* instance)
{
	DEBUG_ANDROID("android_pre_connect");

	rdpSettings* settings = instance->settings;
	BOOL bitmap_cache = settings->BitmapCacheEnabled;
	settings->OrderSupport[NEG_DSTBLT_INDEX] = TRUE;
	settings->OrderSupport[NEG_PATBLT_INDEX] = TRUE;
	settings->OrderSupport[NEG_SCRBLT_INDEX] = TRUE;
	settings->OrderSupport[NEG_OPAQUE_RECT_INDEX] = TRUE;
	settings->OrderSupport[NEG_DRAWNINEGRID_INDEX] = FALSE;
	settings->OrderSupport[NEG_MULTIDSTBLT_INDEX] = FALSE;
	settings->OrderSupport[NEG_MULTIPATBLT_INDEX] = FALSE;
	settings->OrderSupport[NEG_MULTISCRBLT_INDEX] = FALSE;
	settings->OrderSupport[NEG_MULTIOPAQUERECT_INDEX] = TRUE;
	settings->OrderSupport[NEG_MULTI_DRAWNINEGRID_INDEX] = FALSE;
	settings->OrderSupport[NEG_LINETO_INDEX] = TRUE;
	settings->OrderSupport[NEG_POLYLINE_INDEX] = TRUE;
	settings->OrderSupport[NEG_MEMBLT_INDEX] = bitmap_cache;
	settings->OrderSupport[NEG_MEM3BLT_INDEX] = TRUE;
	settings->OrderSupport[NEG_MEMBLT_V2_INDEX] = bitmap_cache;
	settings->OrderSupport[NEG_MEM3BLT_V2_INDEX] = FALSE;
	settings->OrderSupport[NEG_SAVEBITMAP_INDEX] = FALSE;
	settings->OrderSupport[NEG_GLYPH_INDEX_INDEX] = TRUE;
	settings->OrderSupport[NEG_FAST_INDEX_INDEX] = TRUE;
	settings->OrderSupport[NEG_FAST_GLYPH_INDEX] = TRUE;
	settings->OrderSupport[NEG_POLYGON_SC_INDEX] = FALSE;
	settings->OrderSupport[NEG_POLYGON_CB_INDEX] = FALSE;
	settings->OrderSupport[NEG_ELLIPSE_SC_INDEX] = FALSE;
	settings->OrderSupport[NEG_ELLIPSE_CB_INDEX] = FALSE;

	settings->FrameAcknowledge = 10;

	freerdp_channels_load_plugin(instance->context->channels, instance->settings, "tsxlc", NULL);

	freerdp_channels_pre_connect(instance->context->channels, instance);

	return TRUE;
}

BOOL android_post_connect(freerdp* instance)
{
	DEBUG_ANDROID("android_post_connect");

	freerdp_callback("OnSettingsChanged", "(IIII)V", instance, instance->settings->DesktopWidth, instance->settings->DesktopHeight, instance->settings->ColorDepth);

	instance->context->cache = cache_new(instance->settings);

	gdi_init(instance, CLRCONV_ALPHA | ((instance->settings->ColorDepth > 16) ? CLRBUF_32BPP : CLRBUF_16BPP), NULL);

	instance->update->BeginPaint = android_begin_paint;
	instance->update->EndPaint = android_end_paint;
	instance->update->DesktopResize = android_desktop_resize;

	//ai->rail = rail_new(instance->settings);
	//instance->update->rail = (void*) ai->rail;
	//rail_register_update_callbacks(xfi->rail, instance->update);
	//android_rail_register_callbacks(xfi, xfi->rail);

	freerdp_channels_post_connect(instance->context->channels, instance);

	// send notifications 
	freerdp_callback("OnConnectionSuccess", "(I)V", instance);

	return TRUE;
}

jobject create_string_builder(JNIEnv *env, char* initialStr)
{
	jclass cls;
	jmethodID methodId;
	jobject obj;

	// get class
	cls = (*env)->FindClass(env, "java/lang/StringBuilder");
	if(!cls)
		return NULL;

	if(initialStr)
	{
		// get method id for constructor
		methodId = (*env)->GetMethodID(env, cls, "<init>", "(Ljava/lang/String;)V");
		if(!methodId)
			return NULL;

		// create string that holds our initial string
		jstring jstr = (*env)->NewStringUTF(env, initialStr);

		// construct new StringBuilder
		obj = (*env)->NewObject(env, cls, methodId, jstr);
	}
	else
	{
		// get method id for constructor
		methodId = (*env)->GetMethodID(env, cls, "<init>", "()V");
		if(!methodId)
			return NULL;

		// construct new StringBuilder
		obj = (*env)->NewObject(env, cls, methodId);
	}

	return obj;
}

char* get_string_from_string_builder(JNIEnv* env, jobject strBuilder)
{
	jclass cls;
	jmethodID methodId;
	jstring strObj;
	const jbyte* native_str;
	char* result;

	// get class
	cls = (*env)->FindClass(env, "java/lang/StringBuilder");
	if(!cls)
		return NULL;

	// get method id for constructor
	methodId = (*env)->GetMethodID(env, cls, "toString", "()Ljava/lang/String;");
	if(!methodId)
		return NULL;

	// get jstring representation of our buffer
	strObj = (*env)->CallObjectMethod(env, strBuilder, methodId);

	// read string
	native_str = (*env)->GetStringUTFChars(env, strObj, NULL);
	result = strdup(native_str);
	(*env)->ReleaseStringUTFChars(env, strObj, native_str);

	return result;
}


BOOL android_authenticate(freerdp* instance, char** username, char** password, char** domain)
{
	DEBUG_ANDROID("Authenticate user:");
	DEBUG_ANDROID("  Username: %s", *username);
	DEBUG_ANDROID("  Domain: %s", *domain);

	JNIEnv* env;
	jboolean attached = jni_attach_thread(&env);
	jobject jstr1 = create_string_builder(env, *username);
	jobject jstr2 = create_string_builder(env, *domain);
	jobject jstr3 = create_string_builder(env, *password);

	jboolean res = freerdp_callback_bool_result("OnAuthenticate", "(ILjava/lang/StringBuilder;Ljava/lang/StringBuilder;Ljava/lang/StringBuilder;)Z", instance, jstr1, jstr2, jstr3);
	if(res == JNI_TRUE)
	{
		// read back string values
		if(*username != NULL)
			free(*username);

		*username = get_string_from_string_builder(env, jstr1);

		if(*domain != NULL)
			free(*domain);

		*domain = get_string_from_string_builder(env, jstr2);

		if(*password == NULL)
			free(*password);
		
		*password = get_string_from_string_builder(env, jstr3);
	}

	if(attached == JNI_TRUE)
		jni_detach_thread();

	return ((res == JNI_TRUE) ? TRUE : FALSE);
}

BOOL android_verify_certificate(freerdp* instance, char* subject, char* issuer, char* fingerprint)
{
	DEBUG_ANDROID("Certificate details:");
	DEBUG_ANDROID("\tSubject: %s", subject);
	DEBUG_ANDROID("\tIssuer: %s", issuer);
	DEBUG_ANDROID("\tThumbprint: %s", fingerprint);
	DEBUG_ANDROID("The above X.509 certificate could not be verified, possibly because you do not have "
		"the CA certificate in your certificate store, or the certificate has expired."
		"Please look at the documentation on how to create local certificate store for a private CA.\n");

	JNIEnv* env;
	jboolean attached = jni_attach_thread(&env);
	jstring jstr1 = (*env)->NewStringUTF(env, subject);
	jstring jstr2 = (*env)->NewStringUTF(env, issuer);
	jstring jstr3 = (*env)->NewStringUTF(env, fingerprint);

	jboolean res = freerdp_callback_bool_result("OnVerifyCertificate", "(ILjava/lang/String;Ljava/lang/String;Ljava/lang/String;)Z", instance, jstr1, jstr2, jstr3);

	if(attached == JNI_TRUE)
		jni_detach_thread();

	return ((res == JNI_TRUE) ? TRUE : FALSE);
}

BOOL android_verify_changed_certificate(freerdp* instance, char* subject, char* issuer, char* new_fingerprint, char* old_fingerprint)
{
	return android_verify_certificate(instance, subject, issuer, new_fingerprint);
}


/*
int xf_process_plugin_args(rdpSettings* settings, const char* name, RDP_PLUGIN_DATA* plugin_data, void* user_data)
{
	rdpChanMan* chanman = (rdpChanMan*) user_data;

	printf("loading plugin %s\n", name);
	freerdp_chanman_load_plugin(chanman, settings, name, plugin_data);

	return 1;
}
*/

int android_receive_channel_data(freerdp* instance, int channelId, UINT8* data, int size, int flags, int total_size)
{
	return freerdp_channels_data(instance, channelId, data, size, flags, total_size);
}

void android_process_channel_event(rdpChannels* channels, freerdp* instance)
{
	wMessage* event;

	event = freerdp_channels_pop_event(channels);

	if (event)
	{
/*		switch (event->event_class)
		{
			case RailChannel_Class:
				xf_process_rail_event(ai, chanman, event);
				break;

			default:
				break;
		}

		switch (event->event_type)
		{
			case RDP_EVENT_TYPE_CB_SYNC:
				android_process_cb_sync_event(chanman, instance);
				break;
			default:
				break;
		}
*/
		freerdp_event_free(event);
	}
}

int android_freerdp_run(freerdp* instance)
{
	int i;
	int fds;
	int max_fds;
	int rcount;
	int wcount;
	void* rfds[32];
	void* wfds[32];
	fd_set rfds_set;
	fd_set wfds_set;

	memset(rfds, 0, sizeof(rfds));
	memset(wfds, 0, sizeof(wfds));

	if (!freerdp_connect(instance))
	{
		freerdp_callback("OnConnectionFailure", "(I)V", instance);
		return 0;
	}
	
	((androidContext*)instance->context)->is_connected = TRUE;
	while (1)
	{
		rcount = 0;
		wcount = 0;

		if (freerdp_get_fds(instance, rfds, &rcount, wfds, &wcount) != TRUE)
		{
			DEBUG_ANDROID("Failed to get FreeRDP file descriptor\n");
			break;
		}
		if (freerdp_channels_get_fds(instance->context->channels, instance, rfds, &rcount, wfds, &wcount) != TRUE)
		{
			DEBUG_ANDROID("Failed to get channel manager file descriptor\n");
			break;
		}
		if (android_get_fds(instance, rfds, &rcount, wfds, &wcount) != TRUE)
		{
			DEBUG_ANDROID("Failed to get android file descriptor\n");
			break;
		}

		max_fds = 0;
		FD_ZERO(&rfds_set);
		FD_ZERO(&wfds_set);

		for (i = 0; i < rcount; i++)
		{
			fds = (int)(long)(rfds[i]);

			if (fds > max_fds)
				max_fds = fds;

			FD_SET(fds, &rfds_set);
		}

		if (max_fds == 0)
			break;

		if (select(max_fds + 1, &rfds_set, &wfds_set, NULL, NULL) == -1)
		{
			/* these are not really errors */
			if (!((errno == EAGAIN) ||
				(errno == EWOULDBLOCK) ||
				(errno == EINPROGRESS) ||
				(errno == EINTR))) /* signal occurred */
			{
				DEBUG_ANDROID("android_run: select failed\n");
				break;
			}
		}

		if (freerdp_check_fds(instance) != TRUE)
		{
			DEBUG_ANDROID("Failed to check FreeRDP file descriptor\n");
			break;
		}
		if (android_check_fds(instance) != TRUE)
		{
			DEBUG_ANDROID("Failed to check android file descriptor\n");
			break;
		}
		if (freerdp_channels_check_fds(instance->context->channels, instance) != TRUE)
		{
			DEBUG_ANDROID("Failed to check channel manager file descriptor\n");
			break;
		}
		android_process_channel_event(instance->context->channels, instance);
	}

	// issue another OnDisconnecting here in case the disconnect was initiated by the sever and not our client
	freerdp_callback("OnDisconnecting", "(I)V", instance);
	freerdp_channels_close(instance->context->channels, instance);
	freerdp_disconnect(instance);
	gdi_free(instance);
	cache_free(instance->context->cache);
	freerdp_callback("OnDisconnected", "(I)V", instance);

	return 0;
}

void* android_thread_func(void* param)
{
	struct thread_data* data;
	data = (struct thread_data*) param;

	freerdp* instance = data->instance;
	android_freerdp_run(instance);
	free(data);

	pthread_detach(pthread_self());

	return NULL;
}

jint JNI_OnLoad(JavaVM* vm, void* reserved)
{

	DEBUG_ANDROID("JNI_OnLoad");

	jint res = init_callback_environment(vm);

	setlocale(LC_ALL, "");

	freerdp_channels_global_init();

	return res;
}

JNIEXPORT jint JNICALL jni_freerdp_new(JNIEnv *env, jclass cls)
{
	freerdp* instance;

	// create instance
	instance = freerdp_new();
	instance->PreConnect = android_pre_connect;
	instance->PostConnect = android_post_connect;
	instance->Authenticate = android_authenticate;
	instance->VerifyCertificate = android_verify_certificate;
	instance->VerifyChangedCertificate = android_verify_changed_certificate;
	instance->ReceiveChannelData = android_receive_channel_data;
	

	// create context
	instance->context_size = sizeof(androidContext);
	instance->ContextNew = android_context_new;
	instance->ContextFree = android_context_free;
	freerdp_context_new(instance);

	return (jint) instance;
}

JNIEXPORT void JNICALL jni_freerdp_free(JNIEnv *env, jclass cls, jint instance)
{
	freerdp* inst = (freerdp*)instance;
	freerdp_free(inst);
}

JNIEXPORT jboolean JNICALL jni_freerdp_connect(JNIEnv *env, jclass cls, jint instance)
{
	freerdp* inst = (freerdp*)instance;
	struct thread_data* data = (struct thread_data*) malloc(sizeof(struct thread_data));
	data->instance = inst;

	androidContext* ctx = (androidContext*)inst->context;
	pthread_create(&ctx->thread, 0, android_thread_func, data);

	return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL jni_freerdp_disconnect(JNIEnv *env, jclass cls, jint instance)
{
	freerdp* inst = (freerdp*)instance;
	ANDROID_EVENT* event = (ANDROID_EVENT*)android_event_disconnect_new();
	android_push_event(inst, event);
	freerdp_callback("OnDisconnecting", "(I)V", instance);
	return (jboolean) JNI_TRUE;
}

JNIEXPORT void JNICALL jni_freerdp_cancel_connection(JNIEnv *env, jclass cls, jint instance)
{
	DEBUG_ANDROID("Cancelling connection ...");
	freerdp* inst = (freerdp*)instance;
	ANDROID_EVENT* event = (ANDROID_EVENT*)android_event_disconnect_new();
	android_push_event(inst, event);
	freerdp_callback("OnDisconnecting", "(I)V", instance);
}

JNIEXPORT void JNICALL jni_freerdp_set_data_directory(JNIEnv *env, jclass cls, jint instance, jstring jdirectory)
{
	freerdp* inst = (freerdp*)instance;
	rdpSettings * settings = inst->settings;

	const jbyte *directory = (*env)->GetStringUTFChars(env, jdirectory, NULL);

	free(settings->HomePath);
	free(settings->ConfigPath);

	settings->HomePath = strdup(directory);
	settings->ConfigPath = NULL;

	(*env)->ReleaseStringUTFChars(env, jdirectory, directory);
}

JNIEXPORT void JNICALL jni_freerdp_set_connection_info(JNIEnv *env, jclass cls, jint instance,
						   jstring jhostname, jstring jusername, jstring jpassword, jstring jdomain, jint width, jint height,
						   jint color_depth, jint port, jboolean console, jint security, jstring jcertname)
{
	freerdp* inst = (freerdp*)instance;
	rdpSettings * settings = inst->settings;

	const jbyte *hostname = (*env)->GetStringUTFChars(env, jhostname, NULL);
	const jbyte *username = (*env)->GetStringUTFChars(env, jusername, NULL);
	const jbyte *password = (*env)->GetStringUTFChars(env, jpassword, NULL);
	const jbyte *domain = (*env)->GetStringUTFChars(env, jdomain, NULL);
	const jbyte *certname = (*env)->GetStringUTFChars(env, jcertname, NULL);

	DEBUG_ANDROID("hostname: %s", (char*) hostname);
	DEBUG_ANDROID("username: %s", (char*) username);
	DEBUG_ANDROID("password: %s", (char*) password);
	DEBUG_ANDROID("domain: %s", (char*) domain);
	DEBUG_ANDROID("width: %d", width);
	DEBUG_ANDROID("height: %d", height);
	DEBUG_ANDROID("color depth: %d", color_depth);
	DEBUG_ANDROID("port: %d", port);
	DEBUG_ANDROID("security: %d", security);

	settings->DesktopWidth = width;
	settings->DesktopHeight = height;
	settings->ColorDepth = color_depth;
	settings->ServerPort = port;

	// Hack for 16 bit RDVH connections:
	//   In this case we get screen corruptions when we have an odd screen resolution width ... need to investigate what is causing this...
	if (color_depth <= 16)
		settings->DesktopWidth &= (~1);

	settings->ServerHostname = strdup(hostname);

	if(username && strlen(username) > 0)
		settings->Username = strdup(username);

	if(password && strlen(password) > 0)
	{
		settings->Password = strdup(password);
		settings->AutoLogonEnabled = TRUE;
	}

	settings->Domain = strdup(domain);

	if(certname && strlen(certname) > 0)
		settings->CertificateName = strdup(certname);

	settings->ConsoleSession = (console == JNI_TRUE) ? TRUE : FALSE;

	settings->SoftwareGdi = TRUE;

	/* enable NSCodec */
	settings->NSCodec = TRUE;

	switch ((int) security)
	{
		case 1:
			/* Standard RDP */
			settings->RdpSecurity = TRUE;
			settings->TlsSecurity = FALSE;
			settings->NlaSecurity = FALSE;
			settings->ExtSecurity = FALSE;
			settings->DisableEncryption = TRUE;
			settings->EncryptionMethods = ENCRYPTION_METHOD_40BIT | ENCRYPTION_METHOD_128BIT | ENCRYPTION_METHOD_FIPS;
			settings->EncryptionLevel = ENCRYPTION_LEVEL_CLIENT_COMPATIBLE;
			break;

		case 2:
			/* TLS */
			settings->NlaSecurity = FALSE;
			settings->TlsSecurity = TRUE;
			settings->RdpSecurity = FALSE;
			settings->ExtSecurity = FALSE;
			break;

		case 3:
			/* NLA */
			settings->NlaSecurity = TRUE;
			settings->TlsSecurity = FALSE;
			settings->RdpSecurity = FALSE;
			settings->ExtSecurity = FALSE;
			break;

		default:
			break;
	}

	// set US keyboard layout
	settings->KeyboardLayout = 0x0409;

	(*env)->ReleaseStringUTFChars(env, jhostname, hostname);
	(*env)->ReleaseStringUTFChars(env, jusername, username);
	(*env)->ReleaseStringUTFChars(env, jpassword, password);
	(*env)->ReleaseStringUTFChars(env, jdomain, domain);
	(*env)->ReleaseStringUTFChars(env, jcertname, certname);

	return;
}

JNIEXPORT void JNICALL jni_freerdp_set_performance_flags(
	JNIEnv *env, jclass cls, jint instance, jboolean remotefx, jboolean disableWallpaper, jboolean disableFullWindowDrag,
	jboolean disableMenuAnimations, jboolean disableTheming, jboolean enableFontSmoothing, jboolean enableDesktopComposition)
{
	freerdp* inst = (freerdp*)instance;
	rdpSettings * settings = inst->settings;

	DEBUG_ANDROID("remotefx: %d", (remotefx == JNI_TRUE) ? 1 : 0);
	if (remotefx == JNI_TRUE)
	{
		settings->RemoteFxCodec = TRUE;
		settings->FastPathOutput = TRUE;
		settings->ColorDepth = 32;
		settings->LargePointerFlag = TRUE;
		settings->FrameMarkerCommandEnabled = TRUE;
	}

	/* store performance settings */
	if (disableWallpaper == JNI_TRUE)
		settings->DisableWallpaper = TRUE;

	if (disableFullWindowDrag == JNI_TRUE)
		settings->DisableFullWindowDrag = TRUE;

	if (disableMenuAnimations == JNI_TRUE)
		settings->DisableMenuAnims = TRUE;

	if (disableTheming == JNI_TRUE)
		settings->DisableThemes = TRUE;

	if (enableFontSmoothing == JNI_TRUE)
		settings->AllowFontSmoothing = TRUE;

	if(enableDesktopComposition == JNI_TRUE)
		settings->AllowDesktopComposition = TRUE;


	/* Create performance flags from settings */
	settings->PerformanceFlags = PERF_FLAG_NONE;
	if (settings->AllowFontSmoothing)
		settings->PerformanceFlags |= PERF_ENABLE_FONT_SMOOTHING;

	if (settings->AllowDesktopComposition)
		settings->PerformanceFlags |= PERF_ENABLE_DESKTOP_COMPOSITION;

	if (settings->DisableWallpaper)
		settings->PerformanceFlags |= PERF_DISABLE_WALLPAPER;

	if (settings->DisableFullWindowDrag)
		settings->PerformanceFlags |= PERF_DISABLE_FULLWINDOWDRAG;

	if (settings->DisableMenuAnims)
		settings->PerformanceFlags |= PERF_DISABLE_MENUANIMATIONS;

	if (settings->DisableThemes)
		settings->PerformanceFlags |= PERF_DISABLE_THEMING;

	DEBUG_ANDROID("performance_flags: %04X", settings->PerformanceFlags);
}

JNIEXPORT void JNICALL jni_freerdp_set_advanced_settings(JNIEnv *env, jclass cls, jint instance, jstring jRemoteProgram, jstring jWorkDir)
{
	freerdp* inst = (freerdp*)instance;
	rdpSettings * settings = inst->settings;

	const jbyte *remote_program = (*env)->GetStringUTFChars(env, jRemoteProgram, NULL);
	const jbyte *work_dir = (*env)->GetStringUTFChars(env, jWorkDir, NULL);

	DEBUG_ANDROID("Remote Program: %s", (char*) remote_program);
	DEBUG_ANDROID("Work Dir: %s", (char*) work_dir);

	if(remote_program && strlen(remote_program) > 0)
		settings->AlternateShell = strdup(remote_program);

	if(work_dir && strlen(work_dir) > 0)
		settings->ShellWorkingDirectory = strdup(work_dir);

	(*env)->ReleaseStringUTFChars(env, jRemoteProgram, remote_program);
	(*env)->ReleaseStringUTFChars(env, jWorkDir, work_dir);
}

void copy_pixel_buffer(UINT8* dstBuf, UINT8* srcBuf, int x, int y, int width, int height, int wBuf, int hBuf, int bpp)
{
	int i, j;
	int length;
	int scanline;
	UINT8 *dstp, *srcp;
	
	length = width * bpp;
	scanline = wBuf * bpp;

	srcp = (UINT8*) &srcBuf[(scanline * y) + (x * bpp)];
	dstp = (UINT8*) &dstBuf[(scanline * y) + (x * bpp)];

	if (bpp == 4)
	{
		for (i = 0; i < height; i++)
		{
			for (j = 0; j < width * 4; j += 4)
			{
				// ARGB <-> ABGR
				dstp[j + 0] = srcp[j + 2];
				dstp[j + 1] = srcp[j + 1];
				dstp[j + 2] = srcp[j + 0];
				dstp[j + 3] = srcp[j + 3];
			}		

			srcp += scanline;
			dstp += scanline;
		}
	}
	else
	{
		for (i = 0; i < height; i++)
		{
			memcpy(dstp, srcp, length);
			srcp += scanline;
			dstp += scanline;
		}
	}
}

JNIEXPORT jboolean JNICALL jni_freerdp_update_graphics(
	JNIEnv *env, jclass cls, jint instance, jobject bitmap, jint x, jint y, jint width, jint height)
{

	int ret;
	void* pixels;
	AndroidBitmapInfo info;
	freerdp* inst = (freerdp*)instance;
	rdpGdi *gdi = inst->context->gdi;

	if ((ret = AndroidBitmap_getInfo(env, bitmap, &info)) < 0)
	{
		DEBUG_ANDROID("AndroidBitmap_getInfo() failed ! error=%d", ret);
		return JNI_FALSE;
	}

	if ((ret = AndroidBitmap_lockPixels(env, bitmap, &pixels)) < 0)
	{
		DEBUG_ANDROID("AndroidBitmap_lockPixels() failed ! error=%d", ret);
		return JNI_FALSE;
	}

	copy_pixel_buffer(pixels, gdi->primary_buffer, x, y, width, height, gdi->width, gdi->height, gdi->bytesPerPixel);

	AndroidBitmap_unlockPixels(env, bitmap);

	return JNI_TRUE;
}

JNIEXPORT void JNICALL jni_freerdp_send_key_event(
	JNIEnv *env, jclass cls, jint instance, jint keycode, jboolean down)
{
	DWORD scancode;
	ANDROID_EVENT* event;

	freerdp* inst = (freerdp*)instance;

	scancode = GetVirtualScanCodeFromVirtualKeyCode(keycode, 4);
	int flags = (down == JNI_TRUE) ? KBD_FLAGS_DOWN : KBD_FLAGS_RELEASE;
	flags |= (scancode & KBDEXT) ? KBD_FLAGS_EXTENDED : 0;
	event = (ANDROID_EVENT*) android_event_key_new(flags, scancode & 0xFF);

	android_push_event(inst, event);

	DEBUG_ANDROID("send_key_event: %d, %d", scancode, flags);
}

JNIEXPORT void JNICALL jni_freerdp_send_unicodekey_event(
	JNIEnv *env, jclass cls, jint instance, jint keycode)
{
	ANDROID_EVENT* event;

	freerdp* inst = (freerdp*)instance;
	event = (ANDROID_EVENT*) android_event_unicodekey_new(keycode);
	android_push_event(inst, event);

	DEBUG_ANDROID("send_unicodekey_event: %d", keycode);
}

JNIEXPORT void JNICALL jni_freerdp_send_cursor_event(
	JNIEnv *env, jclass cls, jint instance, jint x, jint y, jint flags)
{
	ANDROID_EVENT* event;

	freerdp* inst = (freerdp*)instance;
	event = (ANDROID_EVENT*) android_event_cursor_new(flags, x, y);
	android_push_event(inst, event);

	DEBUG_ANDROID("send_cursor_event: (%d, %d), %d", x, y, flags);
}

JNIEXPORT jstring JNICALL jni_freerdp_get_version(JNIEnv *env, jclass cls)
{
	return (*env)->NewStringUTF(env, GIT_REVISION);
}

