/*
 * From Apple DTS Sample Code.
 */

#if !defined(__DTSSampleCode_GetPID__)
#define __DTSSampleCode_GetPID__ 1

#include <stdlib.h>
#include <stdio.h>

#if defined(__cplusplus)
extern "C" {
#endif
    
enum {
    kSuccess                          =  0,
    kCouldNotFindRequestedProcess     = -1, 
    kInvalidArgumentsError            = -2,
    kErrorGettingSizeOfBufferRequired = -3,
    kUnableToAllocateMemoryForBuffer  = -4,
    kPIDBufferOverrunError            = -5
};

int GetAllPIDsForProcessName(const char*        ProcessName,
                             pid_t              ArrayOfReturnedPIDs[],
                             const unsigned int NumberOfPossiblePIDsInArray,
                             unsigned int*      NumberOfMatchesFound,
                             int*               SysctlError);

int GetPIDForProcessName(const char* ProcessName);

#if defined(__cplusplus)
}
#endif

#endif
