//
// ogles_gpgpu project - GPGPU for mobile devices and embedded systems using OpenGL ES 2.0 
//
// Author: Markus Konrad <post@mkonrad.net>, Winter 2014/2015 
// http://www.mkonrad.net
//
// See LICENSE file in project repository root for the license.
//

#include "og_jni_wrapper.h"

#include "ogles_gpgpu/ogles_gpgpu.h"

#include "og_pipeline.h"

#include <cstdlib>
#include <cassert>
#include <vector>

static ogles_gpgpu::Core *ogCore = NULL;		// ogles_gpgpu core manager instance
static ogles_gpgpu::Disp *ogDisp = NULL;		// ogles_gpgpu render-to-display object

static bool eglInitRequested = false;			// is true if init() is called with initEGL = true
static bool ogInitialized = false;				// is true after init() was called

static jlong outputPxBufNumBytes = 0;			// number of bytes in output buffer
static jobject outputPxBuf = NULL;				// DirectByteBuffer object pointing to <outputPxBufData>
static unsigned char *outputPxBufData = NULL;	// pointer to data in DirectByteBuffer <outputPxBuf>
static jint outputFrameSize[] = { 0, 0 };		// width x height

static GLuint ogInputTexId	= 0;
static GLuint ogOutputTexId	= 0;

void ogCleanupHelper(JNIEnv *env) {
	if (outputPxBuf && outputPxBufData) {	// buffer is already set, release it first
		env->DeleteGlobalRef(outputPxBuf);
		delete outputPxBufData;

		outputPxBuf = NULL;
		outputPxBufData = NULL;
	}
}

JNIEXPORT void JNICALL Java_ogles_1gpgpu_OGJNIWrapper_init(JNIEnv *env, jobject obj, jboolean platOpt, jboolean initEGL, jboolean createRenderDisp) {
	assert(ogCore == NULL);
	OG_LOGINF("OGJNIWrapper", "creating instance of ogles_gpgpu::Core");

	ogCore = ogles_gpgpu::Core::getInstance();

	if (platOpt) {
		ogles_gpgpu::Core::tryEnablePlatformOptimizations();
	}

	// this method is user-defined and sets up the processing pipeline
	ogPipelineSetup(ogCore);
	
	// create a render display output
	if (createRenderDisp) {
		ogDisp = ogCore->createRenderDisplay();
	}

	// initialize EGL context
	if (initEGL && !ogles_gpgpu::EGL::setup()) {
		OG_LOGERR("OGJNIWrapper", "EGL setup failed!");
	}

	eglInitRequested = initEGL;
}

JNIEXPORT void JNICALL Java_ogles_1gpgpu_OGJNIWrapper_cleanup(JNIEnv *env, jobject obj) {
	assert(ogCore);

	OG_LOGINF("OGJNIWrapper", "destroying instance of ogles_gpgpu::Core");

	ogles_gpgpu::Core::destroy();
	ogCore = NULL;

	ogCleanupHelper(env);

	if (eglInitRequested) {
		ogles_gpgpu::EGL::shutdown();
	}
}

JNIEXPORT void JNICALL Java_ogles_1gpgpu_OGJNIWrapper_prepare(JNIEnv *env, jobject obj, jint w, jint h, jboolean prepareInput) {
	assert(ogCore);

	if (eglInitRequested) {
		// set up EGL pixelbuffer surface
		if (!ogles_gpgpu::EGL::createPBufferSurface(w, h)) {
			OG_LOGERR("OGJNIWrapper", "EGL pbuffer creation failed. Aborting!");
			return;
		}

		// activate the EGL context
		if (!ogles_gpgpu::EGL::activate()) {
			OG_LOGERR("OGJNIWrapper", "EGL context activation failed. Aborting!");
			return;
		}
	}

	// initialize ogles_gpgpu
	if (!ogInitialized) {
		ogCore->init();
		ogInitialized = true;
	}

	// prepare for frames of size w by h
	ogCore->prepare(w, h, prepareInput ? GL_RGBA : GL_NONE);

	ogCleanupHelper(env);

	// get the output frame size
	outputFrameSize[0] = ogCore->getOutputFrameW();
	outputFrameSize[1] = ogCore->getOutputFrameH();

	// create the output buffer as NIO direct byte buffer
	outputPxBufNumBytes = outputFrameSize[0] * outputFrameSize[1] * 4;
	outputPxBufData = new unsigned char[outputPxBufNumBytes];
	outputPxBuf = env->NewDirectByteBuffer(outputPxBufData, outputPxBufNumBytes);
	outputPxBuf = env->NewGlobalRef(outputPxBuf);	// we will hold a reference on this global variable until cleanup is called

	// get output texture id
	ogOutputTexId = ogCore->getOutputTexId();

	OG_LOGINF("OGJNIWrapper", "preparation successful. input size is %dx%d, output size is %dx%d",
			w, h, outputFrameSize[0], outputFrameSize[1]);
}

JNIEXPORT void JNICALL Java_ogles_1gpgpu_OGJNIWrapper_setRenderDisp(JNIEnv *env, jobject obj, jint w, jint h, jint orientation) {
	assert(ogInitialized && ogDisp);
	
	ogDisp->setOutputSize(w, h);
	ogDisp->setOutputRenderOrientation((ogles_gpgpu::RenderOrientation)orientation);
}

JNIEXPORT void JNICALL Java_ogles_1gpgpu_OGJNIWrapper_setRenderDispShowMode(JNIEnv *env, jobject obj, jint mode) {
	assert(ogInitialized && ogDisp);
	assert(ogInputTexId > 0);
	assert(ogOutputTexId > 0);
	
	if (mode == ogles_gpgpu_OGJNIWrapper_RENDER_DISP_MODE_INPUT) {
		ogDisp->useTexture(ogInputTexId, 1, GL_TEXTURE_EXTERNAL_OES);
	} else {
		ogDisp->useTexture(ogOutputTexId, 1, GL_TEXTURE_2D);
	}
}

JNIEXPORT void JNICALL Java_ogles_1gpgpu_OGJNIWrapper_setInputPixels(JNIEnv *env, jobject obj, jintArray pxData) {
	assert(ogCore);

	jint *pxInts = env->GetIntArrayElements(pxData, 0);

	assert(pxInts);

	// cast to bytes and set as input data
	ogCore->setInputData((const unsigned char *)pxInts);

	env->ReleaseIntArrayElements(pxData, pxInts, 0);
}

JNIEXPORT void JNICALL Java_ogles_1gpgpu_OGJNIWrapper_setInputTexture(JNIEnv *env, jobject obj, jint texId) {
	ogCore->setInputTexId(texId, GL_TEXTURE_EXTERNAL_OES);
	ogInputTexId = texId;
}

JNIEXPORT jobject JNICALL Java_ogles_1gpgpu_OGJNIWrapper_getOutputPixels(JNIEnv *env, jobject obj) {
	assert(ogCore);

	// write to the output buffer
	ogCore->getOutputData(outputPxBufData);
    
    return outputPxBuf;
}

JNIEXPORT void JNICALL Java_ogles_1gpgpu_OGJNIWrapper_process(JNIEnv *env, jobject obj) {
	assert(ogCore);

	// run the processing operations
	ogCore->process();
}

JNIEXPORT void JNICALL Java_ogles_1gpgpu_OGJNIWrapper_renderOutput(JNIEnv *, jobject) {
	assert(ogInitialized && ogDisp);
	
	ogDisp->render();
}

JNIEXPORT jint JNICALL Java_ogles_1gpgpu_OGJNIWrapper_getOutputFrameW(JNIEnv *env, jobject obj) {
    return outputFrameSize[0];
}

JNIEXPORT jint JNICALL Java_ogles_1gpgpu_OGJNIWrapper_getOutputFrameH(JNIEnv *env, jobject obj) {
	return outputFrameSize[1];
}

JNIEXPORT jdoubleArray JNICALL Java_ogles_1gpgpu_OGJNIWrapper_getTimeMeasurements(JNIEnv *env, jobject obj) {
#ifdef OGLES_GPGPU_BENCHMARK
	std::vector<double> msrmnts = ogCore->getTimeMeasurements();
	size_t num = msrmnts.size();
	jdoubleArray res = env->NewDoubleArray(num);
	jdouble msrmntsArr[num];
	for (size_t i = 0; i < num; ++i) {
		msrmntsArr[i] = msrmnts[i];
	}

	env->SetDoubleArrayRegion(res, 0, num, msrmntsArr);

	return res;

#else
	return NULL;
#endif
}
