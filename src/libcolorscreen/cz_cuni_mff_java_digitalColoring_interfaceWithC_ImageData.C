#include <jni.h>
#include <string.h>
#include "include/colorscreen.h"
#include "cz_cuni_mff_java_digitalColoring_interfaceWithC_ImageData.h"
JNIEXPORT void JNICALL Java_cz_cuni_mff_java_digitalColoring_interfaceWithC_ImageData_load
  (JNIEnv* env, jobject thisObject, jstring filename) {
    jboolean iscopy;
    const char *str = env->GetStringUTFChars(filename, &iscopy);
    image_data *img = new image_data;
    const char *err;

    if (!img->load (str, &err))
      {
	delete img;
        env->ReleaseStringUTFChars (filename, str);
	env->ThrowNew(env->FindClass("java/lang/Exception"), err);
	return;
      }

    env->ReleaseStringUTFChars (filename, str);
    jclass jCls = env->GetObjectClass (thisObject);

    jfieldID widthfld = env->GetFieldID (jCls,"width","I");
    env->SetIntField (thisObject, widthfld, img->width);

    jfieldID heightfld = env->GetFieldID (jCls,"height","I");
    env->SetIntField (thisObject, heightfld, img->height);

    jfieldID maxvalfld = env->GetFieldID (jCls,"maxval","I");
    env->SetIntField (thisObject, maxvalfld, img->maxval);

    jfieldID nativeDatafld = env->GetFieldID (jCls,"nativeData","J");
    env->SetLongField (thisObject, nativeDatafld, (size_t)img);
}

JNIEXPORT jint JNICALL Java_cz_cuni_mff_java_digitalColoring_interfaceWithC_ImageData_getPixelHelper
  (JNIEnv *, jobject, jlong data, jint x, jint y)
{
  image_data *img = (image_data *)(size_t)data;
  return img->data[y][x];
}
