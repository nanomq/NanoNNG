/*
 * Copyright (C) 2023 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <android/binder_parcel.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/cdefs.h>
#include <sys/types.h>

#ifndef __clang__
#define _Nullable
#define _Nonnull
#endif

__BEGIN_DECLS

/*
 * A mapping from string keys to values of various types.
 * See frameworks/base/core/java/android/os/PersistableBundle.java
 * for the Java type than can be used in SDK APIs.
 * APersistableBundle exists to be used in AIDL interfaces and seamlessly
 * interact with framework services.
 * frameworks/native/libs/binder/ndk/include_cpp/android/persistable_bundle_aidl.h
 * contains the AIDL type used in the ndk backend of AIDL interfaces.
 */
struct APersistableBundle;
typedef struct APersistableBundle APersistableBundle;

enum {
    /**
     * This can be returned from functions that need to distinguish between an empty
     * value and a non-existent key.
     */
    APERSISTABLEBUNDLE_KEY_NOT_FOUND = -1,

    /**
     * This can be returned from functions that take a APersistableBundle_stringAllocator.
     * This means the allocator has failed and returned a nullptr.
     */
    APERSISTABLEBUNDLE_ALLOCATOR_FAILED = -2,
};

/**
 * This is a user supplied allocator that allocates a buffer for the
 * APersistableBundle APIs to fill in with a UTF-8 string.
 * The caller that supplies this function is responsible for freeing the
 * returned data.
 *
 * \param the required size in bytes for the allocated buffer
 * \param context pointer if needed by the callback
 *
 * \return allocated buffer of sizeBytes for a UTF-8 string. Null if allocation failed.
 */
typedef char* _Nullable (*_Nonnull APersistableBundle_stringAllocator)(int32_t sizeBytes,
                                                                       void* _Nullable context);

/**
 * Create a new APersistableBundle.
 *
 * Available since API level 202404.
 *
 * \return Pointer to a new APersistableBundle
 */
APersistableBundle* _Nullable APersistableBundle_new() __INTRODUCED_IN(__ANDROID_API_V__);

/**
 * Create a new APersistableBundle based off an existing APersistableBundle.
 * This is a deep copy, so the new APersistableBundle has its own values from
 * copying the original underlying PersistableBundle.
 *
 * Available since API level 202404.
 *
 * \param pBundle to duplicate
 *
 * \return Pointer to a new APersistableBundle
 */
APersistableBundle* _Nullable APersistableBundle_dup(const APersistableBundle* _Nonnull pBundle)
        __INTRODUCED_IN(__ANDROID_API_V__);

/**
 * Delete an APersistableBundle. This must always be called when finished using
 * the object.
 *
 * \param pBundle to delete. No-op if null.
 *
 * Available since API level 202404.
 */
void APersistableBundle_delete(APersistableBundle* _Nullable pBundle)
        __INTRODUCED_IN(__ANDROID_API_V__);

/**
 * Check for equality of APersistableBundles.
 *
 * Available since API level 202404.
 *
 * \param lhs bundle to compare against the other param
 * \param rhs bundle to compare against the other param
 *
 * \return true when equal, false when not
 */
bool APersistableBundle_isEqual(const APersistableBundle* _Nonnull lhs,
                                const APersistableBundle* _Nonnull rhs)
        __INTRODUCED_IN(__ANDROID_API_V__);

/**
 * Read an APersistableBundle from an AParcel.
 *
 * Available since API level 202404.
 *
 * \param parcel to read from
 * \param outPBundle bundle to write to
 *
 * \return STATUS_OK on success
 *         STATUS_BAD_VALUE if the parcel or outBuffer is null, or if there's an
 *                          issue deserializing (eg, corrupted parcel)
 *         STATUS_BAD_TYPE if the parcel's current data position is not that of
 *                         an APersistableBundle type
 *         STATUS_NO_MEMORY if an allocation fails
 */
binder_status_t APersistableBundle_readFromParcel(
        const AParcel* _Nonnull parcel, APersistableBundle* _Nullable* _Nonnull outPBundle)
        __INTRODUCED_IN(__ANDROID_API_V__);

/**
 * Write an APersistableBundle to an AParcel.
 *
 * Available since API level 202404.
 *
 * \param pBundle bundle to write to the parcel
 * \param parcel to write to
 *
 * \return STATUS_OK on success.
 *         STATUS_BAD_VALUE if either pBundle or parcel is null, or if the
 *         APersistableBundle*
 *                          fails to serialize (eg, internally corrupted)
 *         STATUS_NO_MEMORY if the parcel runs out of space to store the pBundle & is
 *                          unable to allocate more
 *         STATUS_FDS_NOT_ALLOWED if the parcel does not allow storing FDs
 */
binder_status_t APersistableBundle_writeToParcel(const APersistableBundle* _Nonnull pBundle,
                                                 AParcel* _Nonnull parcel)
        __INTRODUCED_IN(__ANDROID_API_V__);

/**
 * Get the size of an APersistableBundle. This is the number of mappings in the
 * object.
 *
 * Available since API level 202404.
 *
 * \param pBundle to get the size of (number of mappings)
 *
 * \return number of mappings in the object
 */
int32_t APersistableBundle_size(const APersistableBundle* _Nonnull pBundle)
        __INTRODUCED_IN(__ANDROID_API_V__);

/**
 * Erase any entries added with the provided key.
 *
 * Available since API level 202404.
 *
 * \param pBundle to operate on
 * \param key for the mapping in UTF-8 to erase
 *
 * \return number of entries erased. Either 0 or 1.
 */
int32_t APersistableBundle_erase(APersistableBundle* _Nonnull pBundle, const char* _Nonnull key)
        __INTRODUCED_IN(__ANDROID_API_V__);

/**
 * Put a boolean associated with the provided key.
 * New values with the same key will overwrite existing values.
 *
 * \param pBundle to operate on
 * \param key for the mapping in UTF-8
 * \param value to put for the mapping
 *
 * Available since API level 202404.
 */
void APersistableBundle_putBoolean(APersistableBundle* _Nonnull pBundle, const char* _Nonnull key,
                                   bool val) __INTRODUCED_IN(__ANDROID_API_V__);

/**
 * Put an int32_t associated with the provided key.
 * New values with the same key will overwrite existing values.
 *
 * \param pBundle to operate on
 * \param key for the mapping in UTF-8
 * \param val value to put for the mapping
 *
 * Available since API level 202404.
 */
void APersistableBundle_putInt(APersistableBundle* _Nonnull pBundle, const char* _Nonnull key,
                               int32_t val) __INTRODUCED_IN(__ANDROID_API_V__);

/**
 * Put an int64_t associated with the provided key.
 * New values with the same key will overwrite existing values.
 *
 * \param pBundle to operate on
 * \param key for the mapping in UTF-8
 * \param val value to put for the mapping
 *
 * Available since API level 202404.
 */
void APersistableBundle_putLong(APersistableBundle* _Nonnull pBundle, const char* _Nonnull key,
                                int64_t val) __INTRODUCED_IN(__ANDROID_API_V__);

/**
 * Put a double associated with the provided key.
 * New values with the same key will overwrite existing values.
 *
 * \param pBundle to operate on
 * \param key for the mapping in UTF-8
 * \param val value to put for the mapping
 *
 * Available since API level 202404.
 */
void APersistableBundle_putDouble(APersistableBundle* _Nonnull pBundle, const char* _Nonnull key,
                                  double val) __INTRODUCED_IN(__ANDROID_API_V__);

/**
 * Put a string associated with the provided key.
 * New values with the same key will overwrite existing values.
 * The value is copied.
 *
 * \param pBundle to operate on
 * \param key for the mapping in UTF-8
 * \param vec vector to put for the mapping
 *
 * Available since API level 202404.
 */
void APersistableBundle_putString(APersistableBundle* _Nonnull pBundle, const char* _Nonnull key,
                                  const char* _Nonnull val) __INTRODUCED_IN(__ANDROID_API_V__);

/**
 * Put a boolean vector associated with the provided key.
 * New values with the same key will overwrite existing values.
 * The values are copied.
 *
 * \param pBundle to operate on
 * \param key for the mapping in UTF-8
 * \param vec vector to put for the mapping
 * \param num number of elements in the vector
 *
 * Available since API level 202404.
 */
void APersistableBundle_putBooleanVector(APersistableBundle* _Nonnull pBundle,
                                         const char* _Nonnull key, const bool* _Nonnull vec,
                                         int32_t num) __INTRODUCED_IN(__ANDROID_API_V__);

/**
 * Put an int32_t vector associated with the provided key.
 * New values with the same key will overwrite existing values.
 * The values are copied.
 *
 * \param pBundle to operate on
 * \param key for the mapping in UTF-8
 * \param vec vector to put for the mapping
 * \param num number of elements in the vector
 *
 * Available since API level 202404.
 */
void APersistableBundle_putIntVector(APersistableBundle* _Nonnull pBundle, const char* _Nonnull key,
                                     const int32_t* _Nonnull vec, int32_t num)
        __INTRODUCED_IN(__ANDROID_API_V__);

/**
 * Put an int64_t vector associated with the provided key.
 * New values with the same key will overwrite existing values.
 * The values are copied.
 *
 * \param pBundle to operate on
 * \param key for the mapping in UTF-8
 * \param vec vector to put for the mapping
 * \param num number of elements in the vector
 *
 * Available since API level 202404.
 */
void APersistableBundle_putLongVector(APersistableBundle* _Nonnull pBundle,
                                      const char* _Nonnull key, const int64_t* _Nonnull vec,
                                      int32_t num) __INTRODUCED_IN(__ANDROID_API_V__);

/**
 * Put a double vector associated with the provided key.
 * New values with the same key will overwrite existing values.
 * The values are copied.
 *
 * \param pBundle to operate on
 * \param key for the mapping in UTF-8
 * \param vec vector to put for the mapping
 * \param num number of elements in the vector
 *
 * Available since API level 202404.
 */
void APersistableBundle_putDoubleVector(APersistableBundle* _Nonnull pBundle,
                                        const char* _Nonnull key, const double* _Nonnull vec,
                                        int32_t num) __INTRODUCED_IN(__ANDROID_API_V__);

/**
 * Put a string vector associated with the provided key.
 * New values with the same key will overwrite existing values.
 * The values are copied.
 *
 * \param pBundle to operate on
 * \param key for the mapping in UTF-8
 * \param vec vector to put for the mapping
 * \param num number of elements in the vector
 *
 * Available since API level 202404.
 */
void APersistableBundle_putStringVector(APersistableBundle* _Nonnull pBundle,
                                        const char* _Nonnull key,
                                        const char* _Nullable const* _Nullable vec, int32_t num)
        __INTRODUCED_IN(__ANDROID_API_V__);

/**
 * Put an APersistableBundle associated with the provided key.
 * New values with the same key will overwrite existing values.
 * The value is deep-copied.
 *
 * \param pBundle to operate on
 * \param key for the mapping in UTF-8
 * \param val value to put for the mapping
 *
 * Available since API level 202404.
 */
void APersistableBundle_putPersistableBundle(APersistableBundle* _Nonnull pBundle,
                                             const char* _Nonnull key,
                                             const APersistableBundle* _Nonnull val)
        __INTRODUCED_IN(__ANDROID_API_V__);

/**
 * Get a boolean associated with the provided key.
 *
 * Available since API level 202404.
 *
 * \param pBundle to operate on
 * \param key for the mapping in UTF-8
 * \param val pointer to write the value to
 *
 * \return true if a value exists for the provided key
 */
bool APersistableBundle_getBoolean(const APersistableBundle* _Nonnull pBundle,
                                   const char* _Nonnull key, bool* _Nonnull val)
        __INTRODUCED_IN(__ANDROID_API_V__);

/**
 * Get an int32_t associated with the provided key.
 *
 * Available since API level 202404.
 *
 * \param pBundle to operate on
 * \param key for the mapping in UTF-8
 * \param val pointer to write the value to
 *
 * \return true if a value exists for the provided key
 */
bool APersistableBundle_getInt(const APersistableBundle* _Nonnull pBundle, const char* _Nonnull key,
                               int32_t* _Nonnull val) __INTRODUCED_IN(__ANDROID_API_V__);

/**
 * Get an int64_t associated with the provided key.
 *
 * Available since API level 202404.
 *
 * \param pBundle to operate on
 * \param key for the mapping in UTF-8
 * \param val pointer to write the value to
 *
 * \return true if a value exists for the provided key
 */
bool APersistableBundle_getLong(const APersistableBundle* _Nonnull pBundle,
                                const char* _Nonnull key, int64_t* _Nonnull val)
        __INTRODUCED_IN(__ANDROID_API_V__);

/**
 * Get a double associated with the provided key.
 *
 * Available since API level 202404.
 *
 * \param pBundle to operate on
 * \param key for the mapping in UTF-8
 * \param val pointer to write the value to
 *
 * \return true if a value exists for the provided key
 */
bool APersistableBundle_getDouble(const APersistableBundle* _Nonnull pBundle,
                                  const char* _Nonnull key, double* _Nonnull val)
        __INTRODUCED_IN(__ANDROID_API_V__);

/**
 * Get a string associated with the provided key.
 * The caller is responsible for freeing the returned data.
 *
 * Available since API level 202404.
 *
 * \param pBundle to operate on
 * \param key for the mapping in UTF-8
 * \param val pointer to write the value to in UTF-8
 * \param stringAllocator function pointer to the string allocator
 * \param context pointer that will be passed to the stringAllocator
 *
 * \return size of string in bytes associated with the provided key on success
 *         APERSISTABLEBUNDLE_KEY_NOT_FOUND if the key was not found
 *         APERSISTABLEBUNDLE_ALLOCATOR_FAILED if the provided allocator fails
 */
int32_t APersistableBundle_getString(const APersistableBundle* _Nonnull pBundle,
                                     const char* _Nonnull key, char* _Nullable* _Nonnull val,
                                     APersistableBundle_stringAllocator stringAllocator,
                                     void* _Nullable context) __INTRODUCED_IN(__ANDROID_API_V__);

/**
 * Get a boolean vector associated with the provided key and place it in the
 * provided pre-allocated buffer from the user.
 *
 * This function returns the size in bytes of stored vector.
 * The supplied buffer will be filled in based on the smaller of the supplied
 * bufferSizeBytes or the actual size of the stored data.
 * If the buffer is null or if the supplied bufferSizeBytes is smaller than the
 * actual stored data, then not all of the stored data will be returned.
 *
 * Users can call this function with null buffer and 0 bufferSizeBytes to get
 * the required size of the buffer to use on a subsequent call.
 *
 * \param pBundle to operate on
 * \param key for the mapping in UTF-8
 * \param buffer pointer to a pre-allocated buffer to write the values to
 * \param bufferSizeBytes size of the pre-allocated buffer
 *
 * \return size of the stored vector in bytes. This is the required size of the
 * pre-allocated user supplied buffer if all of the stored contents are desired.
 *         APERSISTABLEBUNDLE_KEY_NOT_FOUND if the key was not found
 */
int32_t APersistableBundle_getBooleanVector(const APersistableBundle* _Nonnull pBundle,
                                            const char* _Nonnull key, bool* _Nullable buffer,
                                            int32_t bufferSizeBytes)
        __INTRODUCED_IN(__ANDROID_API_V__);

/**
 * Get an int32_t vector associated with the provided key and place it in the
 * provided pre-allocated buffer from the user.
 *
 * This function returns the size in bytes of stored vector.
 * The supplied buffer will be filled in based on the smaller of the supplied
 * bufferSizeBytes or the actual size of the stored data.
 * If the buffer is null or if the supplied bufferSizeBytes is smaller than the
 * actual stored data, then not all of the stored data will be returned.
 *
 * Users can call this function with null buffer and 0 bufferSizeBytes to get
 * the required size of the buffer to use on a subsequent call.
 *
 * \param pBundle to operate on
 * \param key for the mapping in UTF-8
 * \param buffer pointer to a pre-allocated buffer to write the values to
 * \param bufferSizeBytes size of the pre-allocated buffer
 *
 * \return size of the stored vector in bytes. This is the required size of the
 * pre-allocated user supplied buffer if all of the stored contents are desired.
 *         APERSISTABLEBUNDLE_KEY_NOT_FOUND if the key was not found
 */
int32_t APersistableBundle_getIntVector(const APersistableBundle* _Nonnull pBundle,
                                        const char* _Nonnull key, int32_t* _Nullable buffer,
                                        int32_t bufferSizeBytes) __INTRODUCED_IN(__ANDROID_API_V__);

/**
 * Get an int64_t vector associated with the provided key and place it in the
 * provided pre-allocated buffer from the user.
 *
 * This function returns the size in bytes of stored vector.
 * The supplied buffer will be filled in based on the smaller of the supplied
 * bufferSizeBytes or the actual size of the stored data.
 * If the buffer is null or if the supplied bufferSizeBytes is smaller than the
 * actual stored data, then not all of the stored data will be returned.
 *
 * Users can call this function with null buffer and 0 bufferSizeBytes to get
 * the required size of the buffer to use on a subsequent call.
 *
 * \param pBundle to operate on
 * \param key for the mapping in UTF-8
 * \param buffer pointer to a pre-allocated buffer to write the values to
 * \param bufferSizeBytes size of the pre-allocated buffer
 *
 * \return size of the stored vector in bytes. This is the required size of the
 * pre-allocated user supplied buffer if all of the stored contents are desired.
 *         APERSISTABLEBUNDLE_KEY_NOT_FOUND if the key was not found
 */
int32_t APersistableBundle_getLongVector(const APersistableBundle* _Nonnull pBundle,
                                         const char* _Nonnull key, int64_t* _Nullable buffer,
                                         int32_t bufferSizeBytes)
        __INTRODUCED_IN(__ANDROID_API_V__);

/**
 * Get a double vector associated with the provided key and place it in the
 * provided pre-allocated buffer from the user.
 *
 * This function returns the size in bytes of stored vector.
 * The supplied buffer will be filled in based on the smaller of the supplied
 * bufferSizeBytes or the actual size of the stored data.
 * If the buffer is null or if the supplied bufferSizeBytes is smaller than the
 * actual stored data, then not all of the stored data will be returned.
 *
 * Users can call this function with null buffer and 0 bufferSizeBytes to get
 * the required size of the buffer to use on a subsequent call.
 *
 * \param pBundle to operate on
 * \param key for the mapping in UTF-8
 * \param buffer pointer to a pre-allocated buffer to write the values to
 * \param bufferSizeBytes size of the pre-allocated buffer
 *
 * \return size of the stored vector in bytes. This is the required size of the
 * pre-allocated user supplied buffer if all of the stored contents are desired.
 *         APERSISTABLEBUNDLE_KEY_NOT_FOUND if the key was not found
 */
int32_t APersistableBundle_getDoubleVector(const APersistableBundle* _Nonnull pBundle,
                                           const char* _Nonnull key, double* _Nullable buffer,
                                           int32_t bufferSizeBytes)
        __INTRODUCED_IN(__ANDROID_API_V__);

/**
 * Get a string vector associated with the provided key and place it in the
 * provided pre-allocated buffer from the user. The user must provide an
 * APersistableBundle_stringAllocator for the individual strings to be
 * allocated.
 * The caller is responsible for freeing the returned data.
 *
 * This function returns the size in bytes of stored vector.
 * The supplied buffer will be filled in based on the smaller of the supplied
 * bufferSizeBytes or the actual size of the stored data.
 * If the buffer is null or if the supplied bufferSizeBytes is smaller than the
 * actual stored data, then not all of the stored data will be returned.
 *
 * Users can call this function with null buffer and 0 bufferSizeBytes to get
 * the required size of the buffer to use on a subsequent call.
 *
 * \param pBundle to operate on
 * \param key for the mapping in UTF-8
 * \param buffer pointer to a pre-allocated buffer to write the string pointers to
 * \param bufferSizeBytes size of the pre-allocated buffer
 * \param stringAllocator function pointer to the string allocator
 * \param context pointer that will be passed to the stringAllocator
 *
 * \return size of the stored vector in bytes. This is the required size of the
 * pre-allocated user supplied buffer if all of the stored contents are desired.
 *         0 if no string vector exists for the provided key
 *         APERSISTABLEBUNDLE_KEY_NOT_FOUND if the key was not found
 *         APERSISTABLEBUNDLE_ALLOCATOR_FAILED if the provided allocator fails
 */
int32_t APersistableBundle_getStringVector(const APersistableBundle* _Nonnull pBundle,
                                           const char* _Nonnull key,
                                           char* _Nullable* _Nullable buffer,
                                           int32_t bufferSizeBytes,
                                           APersistableBundle_stringAllocator stringAllocator,
                                           void* _Nullable context)
        __INTRODUCED_IN(__ANDROID_API_V__);

/**
 * Get an APersistableBundle* associated with the provided key.
 *
 * Available since API level 202404.
 *
 * \param pBundle to operate on
 * \param key for the mapping in UTF-8
 * \param val pointer to an APersistableBundle pointer to write to point to
 * a new copy of the stored APersistableBundle. The caller takes ownership of
 * the new APersistableBundle and must be deleted with
 * APersistableBundle_delete.
 *
 * \return true if a value exists for the provided key
 */
bool APersistableBundle_getPersistableBundle(const APersistableBundle* _Nonnull pBundle,
                                             const char* _Nonnull key,
                                             APersistableBundle* _Nullable* _Nonnull outBundle)
        __INTRODUCED_IN(__ANDROID_API_V__);

/**
 * Get all of the keys associated with this specific type and place it in the
 * provided pre-allocated buffer from the user. The user must provide an
 * APersistableBundle_stringAllocator for the individual strings to be
 * allocated.
 * The caller is responsible for freeing the returned data.
 *
 * This function returns the size in bytes required to fit the fill list of keys.
 * The supplied buffer will be filled in based on the smaller of the supplied
 * bufferSizeBytes or the actual size of the stored data.
 * If the buffer is null or if the supplied bufferSizeBytes is smaller than the
 * actual stored data, then not all of the stored data will be returned.
 *
 * Users can call this function with null buffer and 0 bufferSizeBytes to get
 * the required size of the buffer to use on a subsequent call.
 *
 * \param pBundle to operate on
 * \param outKeys pointer to a pre-allocated buffer to write the UTF-8 keys to
 * \param bufferSizeBytes size of the pre-allocated buffer
 * \param stringAllocator function pointer to the string allocator
 * \param context pointer that will be passed to the stringAllocator
 *
 * \return size of the buffer of keys in bytes. This is the required size of the
 * pre-allocated user supplied buffer if all of the stored contents are desired.
 *         APERSISTABLEBUNDLE_ALLOCATOR_FAILED if the provided allocator fails
 */
int32_t APersistableBundle_getBooleanKeys(const APersistableBundle* _Nonnull pBundle,
                                          char* _Nullable* _Nullable outKeys,
                                          int32_t bufferSizeBytes,
                                          APersistableBundle_stringAllocator stringAllocator,
                                          void* _Nullable context)
        __INTRODUCED_IN(__ANDROID_API_V__);

/**
 * Get all of the keys associated with this specific type and place it in the
 * provided pre-allocated buffer from the user. The user must provide an
 * APersistableBundle_stringAllocator for the individual strings to be
 * allocated.
 * The caller is responsible for freeing the returned data.
 *
 * This function returns the size in bytes required to fit the fill list of keys.
 * The supplied buffer will be filled in based on the smaller of the supplied
 * bufferSizeBytes or the actual size of the stored data.
 * If the buffer is null or if the supplied bufferSizeBytes is smaller than the
 * actual stored data, then not all of the stored data will be returned.
 *
 * Users can call this function with null buffer and 0 bufferSizeBytes to get
 * the required size of the buffer to use on a subsequent call.
 *
 * \param pBundle to operate on
 * \param outKeys pointer to a pre-allocated buffer to write the UTF-8 keys to
 * \param bufferSizeBytes size of the pre-allocated buffer
 * \param stringAllocator function pointer to the string allocator
 * \param context pointer that will be passed to the stringAllocator
 *
 * \return size of the buffer of keys in bytes. This is the required size of the
 * pre-allocated user supplied buffer if all of the stored contents are desired.
 *         APERSISTABLEBUNDLE_ALLOCATOR_FAILED if the provided allocator fails
 */
int32_t APersistableBundle_getIntKeys(const APersistableBundle* _Nonnull pBundle,
                                      char* _Nullable* _Nullable outKeys, int32_t bufferSizeBytes,
                                      APersistableBundle_stringAllocator stringAllocator,
                                      void* _Nullable context) __INTRODUCED_IN(__ANDROID_API_V__);

/**
 * Get all of the keys associated with this specific type and place it in the
 * provided pre-allocated buffer from the user. The user must provide an
 * APersistableBundle_stringAllocator for the individual strings to be
 * allocated.
 * The caller is responsible for freeing the returned data.
 *
 * This function returns the size in bytes required to fit the fill list of keys.
 * The supplied buffer will be filled in based on the smaller of the supplied
 * bufferSizeBytes or the actual size of the stored data.
 * If the buffer is null or if the supplied bufferSizeBytes is smaller than the
 * actual stored data, then not all of the stored data will be returned.
 *
 * Users can call this function with null buffer and 0 bufferSizeBytes to get
 * the required size of the buffer to use on a subsequent call.
 *
 * \param pBundle to operate on
 * \param outKeys pointer to a pre-allocated buffer to write the UTF-8 keys to
 * \param bufferSizeBytes size of the pre-allocated buffer
 * \param stringAllocator function pointer to the string allocator
 * \param context pointer that will be passed to the stringAllocator
 *
 * \return size of the buffer of keys in bytes. This is the required size of the
 * pre-allocated user supplied buffer if all of the stored contents are desired.
 *         APERSISTABLEBUNDLE_ALLOCATOR_FAILED if the provided allocator fails
 */
int32_t APersistableBundle_getLongKeys(const APersistableBundle* _Nonnull pBundle,
                                       char* _Nullable* _Nullable outKeys, int32_t bufferSizeBytes,
                                       APersistableBundle_stringAllocator stringAllocator,
                                       void* _Nullable context) __INTRODUCED_IN(__ANDROID_API_V__);

/**
 * Get all of the keys associated with this specific type and place it in the
 * provided pre-allocated buffer from the user. The user must provide an
 * APersistableBundle_stringAllocator for the individual strings to be
 * allocated.
 * The caller is responsible for freeing the returned data.
 *
 * This function returns the size in bytes required to fit the fill list of keys.
 * The supplied buffer will be filled in based on the smaller of the supplied
 * bufferSizeBytes or the actual size of the stored data.
 * If the buffer is null or if the supplied bufferSizeBytes is smaller than the
 * actual stored data, then not all of the stored data will be returned.
 *
 * Users can call this function with null buffer and 0 bufferSizeBytes to get
 * the required size of the buffer to use on a subsequent call.
 *
 * \param pBundle to operate on
 * \param outKeys pointer to a pre-allocated buffer to write the UTF-8 keys to
 * \param bufferSizeBytes size of the pre-allocated buffer
 * \param stringAllocator function pointer to the string allocator
 * \param context pointer that will be passed to the stringAllocator
 *
 * \return size of the buffer of keys in bytes. This is the required size of the
 * pre-allocated user supplied buffer if all of the stored contents are desired.
 *         APERSISTABLEBUNDLE_ALLOCATOR_FAILED if the provided allocator fails
 */
int32_t APersistableBundle_getDoubleKeys(const APersistableBundle* _Nonnull pBundle,
                                         char* _Nullable* _Nullable outKeys,
                                         int32_t bufferSizeBytes,
                                         APersistableBundle_stringAllocator stringAllocator,
                                         void* _Nullable context)
        __INTRODUCED_IN(__ANDROID_API_V__);

/**
 * Get all of the keys associated with this specific type and place it in the
 * provided pre-allocated buffer from the user. The user must provide an
 * APersistableBundle_stringAllocator for the individual strings to be
 * allocated.
 * The caller is responsible for freeing the returned data.
 *
 * This function returns the size in bytes required to fit the fill list of keys.
 * The supplied buffer will be filled in based on the smaller of the supplied
 * bufferSizeBytes or the actual size of the stored data.
 * If the buffer is null or if the supplied bufferSizeBytes is smaller than the
 * actual stored data, then not all of the stored data will be returned.
 *
 * Users can call this function with null buffer and 0 bufferSizeBytes to get
 * the required size of the buffer to use on a subsequent call.
 *
 * \param pBundle to operate on
 * \param outKeys pointer to a pre-allocated buffer to write the UTF-8 keys to
 * \param bufferSizeBytes size of the pre-allocated buffer
 * \param stringAllocator function pointer to the string allocator
 * \param context pointer that will be passed to the stringAllocator
 *
 * \return size of the buffer of keys in bytes. This is the required size of the
 * pre-allocated user supplied buffer if all of the stored contents are desired.
 *         APERSISTABLEBUNDLE_ALLOCATOR_FAILED if the provided allocator fails
 */
int32_t APersistableBundle_getStringKeys(const APersistableBundle* _Nonnull pBundle,
                                         char* _Nullable* _Nullable outKeys,
                                         int32_t bufferSizeBytes,
                                         APersistableBundle_stringAllocator stringAllocator,
                                         void* _Nullable context)
        __INTRODUCED_IN(__ANDROID_API_V__);

/**
 * Get all of the keys associated with this specific type and place it in the
 * provided pre-allocated buffer from the user. The user must provide an
 * APersistableBundle_stringAllocator for the individual strings to be
 * allocated.
 * The caller is responsible for freeing the returned data.
 *
 * This function returns the size in bytes required to fit the fill list of keys.
 * The supplied buffer will be filled in based on the smaller of the supplied
 * bufferSizeBytes or the actual size of the stored data.
 * If the buffer is null or if the supplied bufferSizeBytes is smaller than the
 * actual stored data, then not all of the stored data will be returned.
 *
 * Users can call this function with null buffer and 0 bufferSizeBytes to get
 * the required size of the buffer to use on a subsequent call.
 *
 * \param pBundle to operate on
 * \param outKeys pointer to a pre-allocated buffer to write the UTF-8 keys to
 * \param bufferSizeBytes size of the pre-allocated buffer
 * \param stringAllocator function pointer to the string allocator
 * \param context pointer that will be passed to the stringAllocator
 *
 * \return size of the buffer of keys in bytes. This is the required size of the
 * pre-allocated user supplied buffer if all of the stored contents are desired.
 *         APERSISTABLEBUNDLE_ALLOCATOR_FAILED if the provided allocator fails
 */
int32_t APersistableBundle_getBooleanVectorKeys(const APersistableBundle* _Nonnull pBundle,
                                                char* _Nullable* _Nullable outKeys,
                                                int32_t bufferSizeBytes,
                                                APersistableBundle_stringAllocator stringAllocator,
                                                void* _Nullable context)
        __INTRODUCED_IN(__ANDROID_API_V__);

/**
 * Get all of the keys associated with this specific type and place it in the
 * provided pre-allocated buffer from the user. The user must provide an
 * APersistableBundle_stringAllocator for the individual strings to be
 * allocated.
 * The caller is responsible for freeing the returned data.
 *
 * This function returns the size in bytes required to fit the fill list of keys.
 * The supplied buffer will be filled in based on the smaller of the supplied
 * bufferSizeBytes or the actual size of the stored data.
 * If the buffer is null or if the supplied bufferSizeBytes is smaller than the
 * actual stored data, then not all of the stored data will be returned.
 *
 * Users can call this function with null buffer and 0 bufferSizeBytes to get
 * the required size of the buffer to use on a subsequent call.
 *
 * \param pBundle to operate on
 * \param outKeys pointer to a pre-allocated buffer to write the UTF-8 keys to
 * \param bufferSizeBytes size of the pre-allocated buffer
 * \param stringAllocator function pointer to the string allocator
 * \param context pointer that will be passed to the stringAllocator
 *
 * \return size of the buffer of keys in bytes. This is the required size of the
 * pre-allocated user supplied buffer if all of the stored contents are desired.
 *         APERSISTABLEBUNDLE_ALLOCATOR_FAILED if the provided allocator fails
 */
int32_t APersistableBundle_getIntVectorKeys(const APersistableBundle* _Nonnull pBundle,
                                            char* _Nullable* _Nullable outKeys,
                                            int32_t bufferSizeBytes,
                                            APersistableBundle_stringAllocator stringAllocator,
                                            void* _Nullable context)
        __INTRODUCED_IN(__ANDROID_API_V__);

/**
 * Get all of the keys associated with this specific type and place it in the
 * provided pre-allocated buffer from the user. The user must provide an
 * APersistableBundle_stringAllocator for the individual strings to be
 * allocated.
 * The caller is responsible for freeing the returned data.
 *
 * This function returns the size in bytes required to fit the fill list of keys.
 * The supplied buffer will be filled in based on the smaller of the supplied
 * bufferSizeBytes or the actual size of the stored data.
 * If the buffer is null or if the supplied bufferSizeBytes is smaller than the
 * actual stored data, then not all of the stored data will be returned.
 *
 * Users can call this function with null buffer and 0 bufferSizeBytes to get
 * the required size of the buffer to use on a subsequent call.
 *
 * \param pBundle to operate on
 * \param outKeys pointer to a pre-allocated buffer to write the UTF-8 keys to
 * \param bufferSizeBytes size of the pre-allocated buffer
 * \param stringAllocator function pointer to the string allocator
 * \param context pointer that will be passed to the stringAllocator
 *
 * \return size of the buffer of keys in bytes. This is the required size of the
 * pre-allocated user supplied buffer if all of the stored contents are desired.
 *         APERSISTABLEBUNDLE_ALLOCATOR_FAILED if the provided allocator fails
 */
int32_t APersistableBundle_getLongVectorKeys(const APersistableBundle* _Nonnull pBundle,
                                             char* _Nullable* _Nullable outKeys,
                                             int32_t bufferSizeBytes,
                                             APersistableBundle_stringAllocator stringAllocator,
                                             void* _Nullable context)
        __INTRODUCED_IN(__ANDROID_API_V__);

/**
 * Get all of the keys associated with this specific type and place it in the
 * provided pre-allocated buffer from the user. The user must provide an
 * APersistableBundle_stringAllocator for the individual strings to be
 * allocated.
 * The caller is responsible for freeing the returned data.
 *
 * This function returns the size in bytes required to fit the fill list of keys.
 * The supplied buffer will be filled in based on the smaller of the supplied
 * bufferSizeBytes or the actual size of the stored data.
 * If the buffer is null or if the supplied bufferSizeBytes is smaller than the
 * actual stored data, then not all of the stored data will be returned.
 *
 * Users can call this function with null buffer and 0 bufferSizeBytes to get
 * the required size of the buffer to use on a subsequent call.
 *
 * \param pBundle to operate on
 * \param outKeys pointer to a pre-allocated buffer to write the UTF-8 keys to
 * \param bufferSizeBytes size of the pre-allocated buffer
 * \param stringAllocator function pointer to the string allocator
 * \param context pointer that will be passed to the stringAllocator
 *
 * \return size of the buffer of keys in bytes. This is the required size of the
 * pre-allocated user supplied buffer if all of the stored contents are desired.
 */
int32_t APersistableBundle_getDoubleVectorKeys(const APersistableBundle* _Nonnull pBundle,
                                               char* _Nullable* _Nullable outKeys,
                                               int32_t bufferSizeBytes,
                                               APersistableBundle_stringAllocator stringAllocator,
                                               void* _Nullable context)
        __INTRODUCED_IN(__ANDROID_API_V__);

/**
 * Get all of the keys associated with this specific type and place it in the
 * provided pre-allocated buffer from the user. The user must provide an
 * APersistableBundle_stringAllocator for the individual strings to be
 * allocated.
 * The caller is responsible for freeing the returned data.
 *
 * This function returns the size in bytes required to fit the fill list of keys.
 * The supplied buffer will be filled in based on the smaller of the supplied
 * bufferSizeBytes or the actual size of the stored data.
 * If the buffer is null or if the supplied bufferSizeBytes is smaller than the
 * actual stored data, then not all of the stored data will be returned.
 *
 * Users can call this function with null buffer and 0 bufferSizeBytes to get
 * the required size of the buffer to use on a subsequent call.
 *
 * \param pBundle to operate on
 * \param outKeys pointer to a pre-allocated buffer to write the UTF-8 keys to
 * \param bufferSizeBytes size of the pre-allocated buffer
 * \param stringAllocator function pointer to the string allocator
 * \param context pointer that will be passed to the stringAllocator
 *
 * \return size of the buffer of keys in bytes. This is the required size of the
 * pre-allocated user supplied buffer if all of the stored contents are desired.
 *         false
 */
int32_t APersistableBundle_getStringVectorKeys(const APersistableBundle* _Nonnull pBundle,
                                               char* _Nullable* _Nullable outKeys,
                                               int32_t bufferSizeBytes,
                                               APersistableBundle_stringAllocator stringAllocator,
                                               void* _Nullable context)
        __INTRODUCED_IN(__ANDROID_API_V__);

/**
 * Get all of the keys associated with this specific type and place it in the
 * provided pre-allocated buffer from the user. The user must provide an
 * APersistableBundle_stringAllocator for the individual strings to be
 * allocated.
 * The caller is responsible for freeing the returned data in bytes.
 *
 * This function returns the size in bytes required to fit the fill list of keys.
 * The supplied buffer will be filled in based on the smaller of the supplied
 * bufferSizeBytes or the actual size of the stored data.
 * If the buffer is null or if the supplied bufferSizeBytes is smaller than the
 * actual stored data, then not all of the stored data will be returned.
 *
 * Users can call this function with null buffer and 0 bufferSizeBytes to get
 * the required size of the buffer to use on a subsequent call.
 *
 * \param pBundle to operate on
 * \param outKeys pointer to a pre-allocated buffer to write the UTF-8 keys to
 * \param bufferSizeBytes size of the pre-allocated buffer
 * \param stringAllocator function pointer to the string allocator
 * \param context pointer that will be passed to the stringAllocator
 *
 * \return size of the buffer of keys in bytes. This is the required size of the
 * pre-allocated user supplied buffer if all of the stored contents are desired.
 *         APERSISTABLEBUNDLE_ALLOCATOR_FAILED if the provided allocator fails
 */
int32_t APersistableBundle_getPersistableBundleKeys(
        const APersistableBundle* _Nonnull pBundle, char* _Nullable* _Nullable outKeys,
        int32_t bufferSizeBytes, APersistableBundle_stringAllocator stringAllocator,
        void* _Nullable context) __INTRODUCED_IN(__ANDROID_API_V__);

__END_DECLS
