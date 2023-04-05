#include "loader.hpp"
#include <unordered_map>
#include <iostream>
#include <sstream>

struct retransform_callback {
	const unsigned char* class_data;
	jint class_data_len;
	bool success;
};

static std::unordered_map<jclass, retransform_callback*> callback_map;

void JNICALL classFileLoadHook(jvmtiEnv* jvmti_env, JNIEnv* env,
	jclass class_being_redefined, jobject loader,
	const char* name, jobject protection_domain,
	jint class_data_len, const unsigned char* class_data,
	jint* new_class_data_len, unsigned char** new_class_data) {

	*new_class_data = NULL;

	if (class_being_redefined)
	{

		for (auto const& [clazz, retransform_callback] : callback_map)
		{
			if (!env->IsSameObject(clazz, class_being_redefined))
			{
				continue;
			}

			callback_map.erase(callback_map.find(clazz));
			retransform_callback->class_data = class_data;
			retransform_callback->class_data_len = class_data_len;
			break;
		}
	}
}

void* allocate(jlong size) {
	void* resultBuffer = nullptr;
	client->jvmti->Allocate(size, (unsigned char**)&resultBuffer);
	return resultBuffer;
}

JNIEXPORT jbyteArray JNICALL GetClassBytes(JNIEnv* env, jclass _, jclass clazz)
{
	retransform_callback retransform_callback;
	callback_map.insert(std::make_pair(clazz, &retransform_callback));

	jclass* classes = (jclass*)allocate(sizeof(jclass));
	classes[0] = clazz;

	jint err = client->jvmti->RetransformClasses(1, classes);

	if (err > 0)
	{
		std::stringstream ss;
		ss << "jvmti error while getting class bytes: ";
		ss << err;

		ERROR_LOG(ss.str().c_str());
		return nullptr;
	}

	jbyteArray output = env->NewByteArray(retransform_callback.class_data_len);
	env->SetByteArrayRegion(output, 0, retransform_callback.class_data_len, (jbyte*)retransform_callback.class_data);

	client->jvmti->Deallocate((unsigned char*)classes);
	return output;
}


JNIEXPORT jint JNICALL RedefineClass(JNIEnv* env, jclass _, jclass clazz, jbyteArray classBytes) {
	jbyte* classByteArray = env->GetByteArrayElements(classBytes, nullptr);
	auto* definitions = (jvmtiClassDefinition*)allocate(sizeof(jvmtiClassDefinition));
	definitions->klass = clazz;
	definitions->class_byte_count = env->GetArrayLength(classBytes);
	definitions->class_bytes = (unsigned char*)classByteArray;

	jint error = (jint)client->jvmti->RedefineClasses(1, definitions);

	env->ReleaseByteArrayElements(classBytes, classByteArray, 0);
	client->jvmti->Deallocate((unsigned char*)definitions);
	return error;
}

JNIEXPORT void JNICALL UnintializeLoader(JNIEnv* env, jclass _)
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