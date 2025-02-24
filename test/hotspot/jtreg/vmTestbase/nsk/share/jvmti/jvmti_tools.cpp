/*
 * Copyright (c) 2003, 2018, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>

/*************************************************************/

#include "jvmti.h"

/*************************************************************/

#include "nsk_tools.h"
#include "jni_tools.h"
#include "jvmti_tools.h"
#include "JVMTITools.h"

/*************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/*************************************************************/

#define NSK_JVMTI_WAITTIME 2

#define NSK_JVMTI_MAX_OPTIONS       10
#define NSK_JVMTI_OPTION_START      '-'
#define NSK_JVMTI_OPTION_VAL_SEP    '='

#define NSK_JVMTI_OPT_PATH_TO_NEW_BYTE_CODE "pathToNewByteCode"
#define PATH_FORMAT "%s%02d/%s"
#define DIR_NAME "newclass"

static volatile int redefineAttempted = NSK_FALSE;
static volatile int redefineSucceed = NSK_FALSE;
static volatile int agentFailed = NSK_FALSE;

static struct {
    struct {
        int count;
        char* names[NSK_JVMTI_MAX_OPTIONS];
        char* values[NSK_JVMTI_MAX_OPTIONS];
        char* string;
    } options;
    int waittime;
} context;

/*************************************************************/

static int check_option(int dashed, const char name[], const char value[]) {
    if (strcmp("verbose", name) == 0) {
        if (strlen(value) > 0) {
            nsk_complain("nsk_jvmti_parseOptions(): unexpected value in option: %s=%s\n", name, value);
            return NSK_FALSE;
        }
        nsk_setVerboseMode(NSK_TRUE);
    } else if (strcmp("trace", name) == 0) {
        if (strlen(value) <= 0) {
            nsk_complain("nsk_jvmti_parseOptions(): no value for option: %s\n", name);
            return NSK_FALSE;
        }
        if (strcmp("none", value) == 0) {
            nsk_setTraceMode(NSK_TRACE_NONE);
        } else if (strcmp("before", value) == 0) {
            nsk_setTraceMode(NSK_TRACE_BEFORE);
        } else if (strcmp("after", value) == 0) {
            nsk_setTraceMode(NSK_TRACE_AFTER);
        } else if (strcmp("all", value) == 0) {
            nsk_setTraceMode(NSK_TRACE_ALL);
        } else {
            nsk_complain("nsk_jvmti_parseOptions(): uexpected value in option: %s=%s\n", name, value);
            return NSK_FALSE;
        }
        nsk_setVerboseMode(NSK_TRUE);
    } else if (strcmp("waittime", name) == 0) {
        if (strlen(value) <= 0) {
            nsk_complain("nsk_jvmti_parseOptions(): no value for option: %s\n", name);
            return NSK_FALSE;
        }
        {
            char* end = NULL;
            long n = strtol(value, &end, 10);
            if (end == NULL || end == value || *end != '\0') {
                nsk_complain("nsk_jvmti_parseOptions(): not integer value in option: %s=%s\n", name, value);
                return NSK_FALSE;
            }
            if (n < 0) {
                nsk_complain("nsk_jvmti_parseOptions(): negative value in option: %s=%s\n", name, value);
                return NSK_FALSE;
            }
            context.waittime = (int)n;
        }
    } else if (dashed) {
        nsk_complain("nsk_jvmti_parseOptions(): unknown option: %c%s\n",
                                                        NSK_JVMTI_OPTION_START, name);
        return NSK_FALSE;
    }
    return NSK_TRUE;
}

static int add_option(const char opt[], int opt_len, const char val[], int val_len) {
    char* name;
    char* value;

    int success = NSK_TRUE;
    int dashed_opt = NSK_FALSE;

    if (opt[0] == NSK_JVMTI_OPTION_START) {
        dashed_opt = NSK_TRUE;
        opt++;
        opt_len--;
    }
    if (opt_len <= 0) {
        nsk_complain("nsk_jvmti_parseOptions(): found empty option\n");
        return NSK_FALSE;
    }

    name = (char*)malloc(opt_len + 1);
    value = (char*)malloc(val_len + 1);

    if (name == NULL || value == NULL) {
        nsk_complain("nsk_jvmti_parseOptions(): out of memory\n");
        success = NSK_FALSE;
    } else {
        strncpy(name, opt, opt_len);
        name[opt_len] = '\0';
        strncpy(value, val, val_len);
        value[val_len] = '\0';

        if (!check_option(dashed_opt, name, value)) {
            success = NSK_FALSE;
        }
    }

    if (success) {
        if (context.options.count >= NSK_JVMTI_MAX_OPTIONS) {
            nsk_complain("nsk_jvmti_parseOptions(): too many options for parsing\n");
            success = NSK_FALSE;
        } else {
            context.options.names[context.options.count] = name;
            context.options.values[context.options.count] = value;
            context.options.count++;
        }
    }

    if (!success) {
        if (name != NULL)
            free(name);
        if (value != NULL)
            free(value);
    }

    return success;
}

static void nsk_jvmti_free() {
    if (context.options.count > 0) {
        int i;
        for (i = 0; i < context.options.count; i++) {
            free(context.options.names[i]);
            free(context.options.values[i]);
        }
        context.options.count = 0;
    }
    if (context.options.string != NULL) {
        free(context.options.string);
        context.options.string = NULL;
    }
}

int isOptSep(char c) {
    return isspace(c) || c == '~';
}


/**
 *
 * The current option will not perform more than one
 * single option which given, this is due to places explained
 * in this question.
 *
 **/

 /*
  * This whole play can be reduced with simple StringTokenizer (strtok).
  *
  */

#if !defined(__clang_major__) && defined(__GNUC__) && (__GNUC__ >= 8)
_Pragma("GCC diagnostic push")
_Pragma("GCC diagnostic ignored \"-Wstringop-truncation\"")
#endif

int nsk_jvmti_parseOptions(const char options[]) {
    size_t len;
    const char* opt;
    int success = NSK_TRUE;

    context.options.string = NULL;
    context.options.count = 0;
    context.waittime = 2;

    if (options == NULL)
        return NSK_TRUE;

    len = strlen(options);
    context.options.string = (char*)malloc(len + 2);

    if (context.options.string == NULL) {
            nsk_complain("nsk_jvmti_parseOptions(): out of memory\n");
            return NSK_FALSE;
    }
    strncpy(context.options.string, options, len);
    context.options.string[len] = '\0';
    context.options.string[len+1] = '\0';

    for (opt = context.options.string; ; ) {
        const char* opt_end;
        const char* val_sep;
        int opt_len=0;
        int val_len=0;
                int exit=1;

        while (*opt != '\0' && isOptSep(*opt)) opt++;
        if (*opt == '\0') break;

        val_sep = NULL;
        /*
            This should break when the first option it encounters other wise
        */
        for (opt_end = opt, opt_len=0; !(*opt_end == '\0' || isOptSep(*opt_end)); opt_end++,opt_len++) {
            if (*opt_end == NSK_JVMTI_OPTION_VAL_SEP) {
                val_sep = opt_end;
                exit=0;
                break;
            }
        }

        if (exit == 1) break;

        /* now scan for the search  for the option value end.

        */
        exit =1;
        opt_end++;
        val_sep++;
        /**
         * I was expecting this jvmti_parseOptions(),
         * should be for multiple options as well.
         * If this break is not there then It will expects
         * to have. so a space should be sufficient as well.
         */
        for(val_len=0; !(*opt_end == '\0' || isOptSep(*opt_end)); opt_end++,val_len++) {
            //if (*opt_end == NSK_JVMTI_OPTION_START) {
            //    break;
            //}
        }

        if (!add_option(opt, opt_len, val_sep, val_len)) {
            success = NSK_FALSE;
            break;
        }
        opt_end++;
        opt = opt_end;
    }

    if (!success) {
        nsk_jvmti_free();
    }

    return success;
}

#if !defined(__clang_major__) && defined(__GNUC__) && (__GNUC__ >= 8)
_Pragma("GCC diagnostic pop")
#endif

/*************************************************************/

/**
 * Returns value of given option name; or NULL if no such option found.
 * If search name is NULL then complains an error and returns NULL.
 */
const char* nsk_jvmti_findOptionValue(const char name[]) {
    int i;

    if (name == NULL) {
        nsk_complain("nsk_jvmti_findOptionValue(): option name is NULL\n");
        return NULL;
    }

    for (i = 0; i < context.options.count; i++) {
        if (strcmp(name, context.options.names[i]) == 0)
            return context.options.values[i];
    }
    return NULL;
}

/**
 * Returns string value of given option; or defaultValue if no such option found.
 * If options is specified but has empty value then complains an error and returns NULL.
 */
const char* nsk_jvmti_findOptionStringValue(const char name[], const char* defaultValue) {
    const char* value;

    if (name == NULL) {
        nsk_complain("nsk_jvmti_findOptionStringValue(): option name is NULL\n");
        return NULL;
    }

    value = nsk_jvmti_findOptionValue(name);
    if (value == NULL) {
        return defaultValue;
    }

    if (strlen(value) <= 0) {
        nsk_complain("nsk_jvmti_findOptionStringValue(): empty value of option: %s=%s\n",
                                                                            name, value);
        return NULL;
    }
    return value;
}

/**
 * Returns integer value of given option; or defaultValue if no such option found.
 * If options is specified but has no integer value then complains an error and returns -1.
 */
int nsk_jvmti_findOptionIntValue(const char name[], int defaultValue) {
    const char* value;

    if (name == NULL) {
        nsk_complain("nsk_jvmti_findOptionIntValue(): option name is NULL\n");
        return -1;
    }

    value = nsk_jvmti_findOptionValue(name);
    if (value == NULL) {
        return defaultValue;
    }

    if (strlen(value) <= 0) {
        nsk_complain("nsk_jvmti_findOptionIntValue(): empty value of option: %s=%s\n",
                                                                            name, value);
        return -1;
    }

    {
        char* endptr = NULL;
        int n = strtol(value, &endptr, 10);

        if (endptr == NULL || *endptr != '\0') {
            nsk_complain("nsk_jvmti_findOptionIntValue(): not integer value of option: %s=%s\n",
                                                                            name, value);
            return -1;
        }
        return n;
    }
}

/**
 * Returns number of parsed options.
 */
int nsk_jvmti_getOptionsCount() {
    return context.options.count;
}

/**
 * Returns name of i-th parsed option.
 * If no such option then complains an error and returns NULL.
 */
const char* nsk_jvmti_getOptionName(int i) {
    if (i < 0 || i >= context.options.count) {
        nsk_complain("nsk_jvmti_getOptionName(): option index out of bounds: %d\n", i);
        return NULL;
    }
    return context.options.names[i];
}

/**
 * Returns value of i-th parsed option.
 * If no such option then complains an error and returns NULL.
 */
const char* nsk_jvmti_getOptionValue(int i) {
    if (i < 0 || i >= context.options.count) {
        nsk_complain("nsk_jvmti_getOptionValue(): option index out of bounds: %d\n", i);
        return NULL;
    }
    return context.options.values[i];
}

/*************************************************************/

/**
 * Returns value of -waittime option or default value if not specified.
 */
int  nsk_jvmti_getWaitTime() {
    return context.waittime;
}

/**
 * Sets specified waittime value.
 */
void nsk_jvmti_setWaitTime(int waittime) {
    context.waittime = waittime;
}

/*************************************************************/

int nsk_jvmti_lverify(int positive, jvmtiError error, jvmtiError expected,
                        const char file[], int line, const char format[], ...)
{
    int failure=0;
    int negative = !positive;
    int errorCode = (int)error;
    const char* errorName = TranslateError(error);
    va_list ap;
    va_start(ap,format);
    nsk_lvtrace(NSK_TRACE_AFTER,file,line,format,ap);
    if (negative || expected != JVMTI_ERROR_NONE)
        nsk_ltrace(NSK_TRACE_AFTER,file,line,
            "  jvmti error: code=%d, name=%s\n",errorCode,errorName);
    if ((error == expected) == negative) {
        nsk_lvcomplain(file,line,format,ap);
        nsk_printf("#   jvmti error: code=%d, name=%s\n",errorCode,errorName);
        if (expected != JVMTI_ERROR_NONE)
            nsk_printf("#   error expected: code=%d, name=%s\n",
                expected, TranslateError(expected));
        failure=1;
    };
    va_end(ap);
    return !failure;
}

/*************************************************************/

JNIEXPORT jstring JNICALL
Java_nsk_share_jvmti_ArgumentHandler_getAgentOptionsString(JNIEnv *jni, jobject obj) {
    jstring str_obj = NULL;

    if (!NSK_JNI_VERIFY(jni, (str_obj =
            NSK_CPP_STUB2(NewStringUTF, jni, context.options.string)) != NULL)) {
        return NULL;
    }
    return str_obj;
}

/*************************************************************/

/**
  * This method will try to redefine the class (classToRedefine) by loading
  * physical file.  <b>pathToNewByteCode</b> option which is passed
  * on OnLoad Phase also used.
  *
  * So This method will do a file read pathToByteCode+fileName+.class (total path).
  * Constrcuts a class objects and does a redefine of the class.
  * On successfull redefine this method will return eaither JNI_TRUE or JNI_FALSE
  *
  * Hint::
  *     1)
  *      If there are many redefine on same testcase, then please try to use
  *      integer value (newclass00, newclass01, newclass02 , ....) way.
  *
  *     2) When you compile these please do keep, a metatag on testcase as
  *     # build : native classes classes.redef
  *
  *     3) When you do build these classes are psysically located in build as.
  *
  * TESTBASE/bin/newclass0* directory.
  * eg: for nks/jvmti/scenarios/hotswap/HS204/hs204t001 you should see
  * TESTBASE/bin/newclass0* /nsk/hotswap/HS204/hs204t001/MyClass.class
  *
  */

int nsk_jvmti_redefineClass(jvmtiEnv * jvmti,
        jclass classToRedefine,
        const char * fileName) {
    redefineAttempted = NSK_TRUE;
    if ( nsk_jvmti_findOptionValue(NSK_JVMTI_OPT_PATH_TO_NEW_BYTE_CODE)
            == NULL  ) {
        nsk_printf("#   error expected: %s \n",
                NSK_JVMTI_OPT_PATH_TO_NEW_BYTE_CODE );
        nsk_printf("Hint :: missing java -agentlib:agentlib=%s=DirName, ($TESTBASE/bin) \n",
                NSK_JVMTI_OPT_PATH_TO_NEW_BYTE_CODE );
        return NSK_FALSE;
    }
    if ( fileName == NULL) {
        nsk_printf("# error file name expected did not found \n");
        return NSK_FALSE;
    }
    {
        char file [1024];
        //= "DEFAULT";
        sprintf(file,"%s/%s.class",
                nsk_jvmti_findOptionValue(NSK_JVMTI_OPT_PATH_TO_NEW_BYTE_CODE),
                fileName);
        nsk_printf("# info :: File = %s \n",file);

        {
            FILE *bytecode;
            unsigned char * classBytes;
            jvmtiError error;
            jint size;

            bytecode = fopen(file, "rb");
            error= JVMTI_ERROR_NONE;
            if ( bytecode == NULL ) {
                nsk_printf("# error **Agent::error opening file %s \n",file);
                return NSK_FALSE;
            }

            nsk_printf("#  info **Agent:: opening file %s \n",file);
            fseek(bytecode, 0, SEEK_END);
            size = ftell(bytecode);
            nsk_printf("# info file size= %ld\n",ftell(bytecode));
            rewind(bytecode);
            error = jvmti->Allocate(size,&classBytes);
            if ( error != JVMTI_ERROR_NONE) {
                nsk_printf(" Failed to create memory %s \n",TranslateError(error));
                return NSK_FALSE;
            }

            if ( ((jint) fread( classBytes, 1,size,bytecode )) != size ) {
                nsk_printf(" # error failed to read all the bytes , could be less or more \n");
                return NSK_FALSE;
            } else {
                nsk_printf(" File red completely \n");
            }
            fclose(bytecode);
            {
                jvmtiClassDefinition classDef;
                classDef.klass = classToRedefine;
                classDef.class_byte_count= size;
                classDef.class_bytes = classBytes;
                error = jvmti->RedefineClasses(1,&classDef);
                if ( error != JVMTI_ERROR_NONE ) {
                    nsk_printf("# error occured while redefining %s ",
                            TranslateError(error) );
                    return NSK_FALSE;
                }
            }
        }
    }
    redefineSucceed= NSK_TRUE;
    return NSK_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_nsk_share_jvmti_RedefineAgent_redefineAttempted(JNIEnv *jni,  jobject obj) {

    if (redefineAttempted == NSK_TRUE) {
        return JNI_TRUE;
    }else {
        return JNI_FALSE;
    }
}


JNIEXPORT jboolean JNICALL
Java_nsk_share_jvmti_RedefineAgent_isRedefined(JNIEnv * jni,  jobject obj ) {

    if (redefineSucceed == NSK_TRUE) {
        return JNI_TRUE;
    }else {
        return JNI_FALSE;
    }
}
/**
 * This jni method is a Java wrapper for agent status.
 */
JNIEXPORT jboolean JNICALL
Java_nsk_share_jvmti_RedefineAgent_agentStatus(JNIEnv * jni,  jobject obj ) {
    if ( agentFailed == NSK_TRUE) {
        return JNI_FALSE;
    } else {
        return JNI_TRUE;
    }
}

void nsk_jvmti_getFileName(int redefineCnt, const char * dir,  char * buf, size_t bufsize) {
   snprintf(buf, bufsize, PATH_FORMAT, DIR_NAME, redefineCnt, dir);
   buf[bufsize-1] = '\0';
}

int nsk_jvmti_enableNotification(jvmtiEnv *jvmti,jvmtiEvent event, jthread thread) {
    jvmtiError rc=JVMTI_ERROR_NONE;
    rc = jvmti->SetEventNotificationMode(JVMTI_ENABLE, event, thread);
    if (rc != JVMTI_ERROR_NONE) {
        nsk_printf("# error Failed to set Notification for Event \n ");
        return NSK_FALSE;
    }
    return NSK_TRUE;
}

int nsk_jvmti_disableNotification(jvmtiEnv *jvmti,jvmtiEvent event, jthread thread) {
  jvmtiError rc=JVMTI_ERROR_NONE;
  rc = jvmti->SetEventNotificationMode(JVMTI_DISABLE, event, thread);
  if (rc != JVMTI_ERROR_NONE) {
      nsk_printf(" Failed to disaable Notification for Event ");
      return NSK_FALSE;
  }
  return NSK_TRUE;
}

void nsk_jvmti_agentFailed() {
    agentFailed = NSK_TRUE;
}

int isThreadExpected(jvmtiEnv *jvmti, jthread thread) {
    static const char *vm_jfr_buffer_thread_name = "VM JFR Buffer Thread";
    static const char *jfr_request_timer_thread_name = "JFR request timer";

    jvmtiThreadInfo threadinfo;
    NSK_JVMTI_VERIFY(jvmti->GetThreadInfo(thread, &threadinfo));

    if (strcmp(threadinfo.name, vm_jfr_buffer_thread_name) == 0)
        return 0;

    if (strcmp(threadinfo.name, jfr_request_timer_thread_name) == 0)
        return 0;

    return 1;
}

jint createRawMonitor(jvmtiEnv *env, const char *name, jrawMonitorID *monitor) {
    jvmtiError error = NSK_CPP_STUB3(CreateRawMonitor, env, name, monitor);
    if (!NSK_JVMTI_VERIFY(error)) {
        return JNI_ERR;
    }
    return JNI_OK;
}

void exitOnError(jvmtiError error) {
    if (!NSK_JVMTI_VERIFY(error)) {
        exit(error);
    }
}

void rawMonitorEnter(jvmtiEnv *env, jrawMonitorID monitor) {
    jvmtiError error = NSK_CPP_STUB2(RawMonitorEnter, env, monitor);
    exitOnError(error);
}

void rawMonitorExit(jvmtiEnv *env, jrawMonitorID monitor) {
    jvmtiError error = NSK_CPP_STUB2(RawMonitorExit, env, monitor);
    exitOnError(error);
}

void rawMonitorNotify(jvmtiEnv *env, jrawMonitorID monitor) {
    jvmtiError error = NSK_CPP_STUB2(RawMonitorNotify, env, monitor);
    exitOnError(error);
}

void rawMonitorWait(jvmtiEnv *env, jrawMonitorID monitor, jlong millis) {
    jvmtiError error = NSK_CPP_STUB3(RawMonitorWait, env, monitor, millis);
    exitOnError(error);
}

void getPhase(jvmtiEnv *env, jvmtiPhase *phase) {
    jvmtiError error = NSK_CPP_STUB2(GetPhase, env, phase);
    exitOnError(error);
}

/*************************************************************/

#ifdef __cplusplus
}
#endif
