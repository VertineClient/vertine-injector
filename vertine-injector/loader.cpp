#include "loader.hpp"

static jclass class_to_transform = nullptr;
static unsigned char* class_bytes;
static int class_bytes_len;

void JNICALL classFileLoadHook(jvmtiEnv* jvmti_env, JNIEnv* env,
	jclass class_being_redefined, jobject loader,
	const char* name, jobject protection_domain,
	jint class_data_len, const unsigned char* class_data,
	jint* new_class_data_len, unsigned char** new_class_data) {

	*new_class_data = NULL;

	if (class_to_transform && class_being_redefined && env->IsSameObject(class_being_redefined, class_to_transform))
	{
		class_bytes = (unsigned char*)class_data;
		class_bytes_len = class_data_len;
	}
}

JNIEXPORT jbyteArray JNICALL GetClassBytes(JNIEnv* env, jclass _, jclass clazz)
{
	class_to_transform = clazz;
	class_bytes = nullptr;

	jclass* classes = (jclass*)malloc(sizeof(jclass));
	classes[0] = clazz;

	client->jvmti->RetransformClasses(1, classes);

	while (!class_bytes)
	{
		continue;
	}

	jbyteArray outputArray = env->NewByteArray(class_bytes_len);
	env->SetByteArrayRegion(outputArray, 0, class_bytes_len, (jbyte*)class_bytes);

	free(classes);
	return outputArray;
}

void* allocate(jlong size) {
	void* resultBuffer = nullptr;
	client->jvmti->Allocate(size, (unsigned char**)&resultBuffer);
	return resultBuffer;
}


JNIEXPORT jint JNICALL RedefineClass(JNIEnv* env, jclass clazz, jclass classToRedefine, jbyteArray classBytes) {
	jint error;
	jbyte* classByteArray = env->GetByteArrayElements(classBytes, nullptr);
	auto* definitions = (jvmtiClassDefinition*)allocate(sizeof(jvmtiClassDefinition));
	definitions->klass = classToRedefine;
	definitions->class_byte_count = env->GetArrayLength(classBytes);
	definitions->class_bytes = (unsigned char*)classByteArray;

	error = (jint)client->jvmti->RedefineClasses(1, definitions);

	env->ReleaseByteArrayElements(classBytes, classByteArray, 0);
	client->jvmti->Deallocate((unsigned char*)definitions);
	return error;
}

JNIEXPORT void JNICALL UnintializeLoader(JNIEnv* env, jclass clazz)
{
	client->done = true;
}

void loader::initialize_loader()
{
	jvmtiCapabilities capabilities;
	memset(&capabilities, 0, sizeof(capabilities));

	capabilities.can_retransform_classes = true;
	capabilities.can_retransform_any_class = true;
	capabilities.can_redefine_any_class = true;
	capabilities.can_redefine_classes = true;
	capabilities.can_generate_all_class_hook_events = true;

	client->jvmti->AddCapabilities(&capabilities);

	jvmtiEventCallbacks callbacks;
	memset(&callbacks, 0, sizeof(callbacks));
	callbacks.ClassFileLoadHook = &classFileLoadHook;

	client->jvmti->SetEventCallbacks(&callbacks, sizeof(callbacks));
	client->jvmti->SetEventNotificationMode(JVMTI_ENABLE, JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, NULL);

	jclass c_loader = client->classloader->find_class(LOADER_CLASS);

	JNINativeMethod methods[] =
	{
		{"GetClassBytes", "(Ljava/lang/Class;)[B", (void*)&GetClassBytes},
		{"RedefineClass", "(Ljava/lang/Class;[B)I", (void*)&RedefineClass},
		{"UnintializeLoader", "()V", (void*)&UnintializeLoader}
	};

	client->env->RegisterNatives(c_loader, methods, 3);
}

void loader::uninitialize_loader()
{
	jclass c_loader = client->classloader->find_class(LOADER_CLASS);
	client->env->UnregisterNatives(c_loader);

	client->jvmti->SetEventNotificationMode(JVMTI_DISABLE, JVMTI_EVENT_CLASS_FILE_LOAD_HOOK, NULL);
}