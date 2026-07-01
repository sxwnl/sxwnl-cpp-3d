/*
** Copyright (c) 2008-2018 The Khronos Group Inc.
** SPDX-License-Identifier: MIT
*/
#ifndef __khrplatform_h_
#define __khrplatform_h_

#include <stddef.h>

typedef signed   char          khronos_int8_t;
typedef unsigned char          khronos_uint8_t;
typedef signed   short int     khronos_int16_t;
typedef unsigned short int     khronos_uint16_t;
typedef signed   int           khronos_int32_t;
typedef unsigned int           khronos_uint32_t;
#if defined(_WIN64) || defined(__LP64__) || defined(__x86_64__)
typedef signed   long long int khronos_int64_t;
typedef unsigned long long int khronos_uint64_t;
typedef signed   long long int khronos_intptr_t;
typedef unsigned long long int khronos_uintptr_t;
typedef signed   long long int khronos_ssize_t;
typedef unsigned long long int khronos_usize_t;
#else
typedef signed   long  int     khronos_int64_t;
typedef unsigned long  int     khronos_uint64_t;
typedef signed   long  int     khronos_intptr_t;
typedef unsigned long  int     khronos_uintptr_t;
typedef signed   long  int     khronos_ssize_t;
typedef unsigned long  int     khronos_usize_t;
#endif

typedef          ptrdiff_t     khronos_ptrdiff_t;
typedef float                  khronos_float_t;
typedef double                 khronos_double_t;

#ifndef KHRONOS_APICALL
#  ifdef _WIN32
#    define KHRONOS_APICALL __declspec(dllimport)
#  else
#    define KHRONOS_APICALL
#  endif
#endif
#ifndef KHRONOS_APIENTRY
#  if defined(_WIN32)
#    define KHRONOS_APIENTRY __stdcall
#  else
#    define KHRONOS_APIENTRY
#  endif
#endif
#ifndef KHRONOS_APIATTRIBUTES
#  define KHRONOS_APIATTRIBUTES
#endif

#endif /* __khrplatform_h_ */
