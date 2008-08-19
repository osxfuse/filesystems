/*
 * From Apple DTS Sample Code.
 */

#include "GetPID.h"

#include <errno.h>
#include <string.h>
#include <sys/sysctl.h>

int
GetAllPIDsForProcessName(const char*        ProcessName, 
                         pid_t              ArrayOfReturnedPIDs[], 
                         const unsigned int NumberOfPossiblePIDsInArray, 
                         unsigned int*      NumberOfMatchesFound,
                         int*               SysctlError)
{
    int    mib[6] = { 0,0,0,0,0,0 };
    int    SuccessfullyGotProcessInformation;
    size_t sizeOfBufferRequired = 0;
    int    error = 0;
    long   NumberOfRunningProcesses = 0;
    int    Counter = 0;
    struct kinfo_proc* BSDProcessInformationStructure = NULL;
    pid_t  CurrentExaminedProcessPID = 0;
    char*  CurrentExaminedProcessName = NULL;

    if (ProcessName == NULL) {
        return kInvalidArgumentsError;
    }

    if (ArrayOfReturnedPIDs == NULL) {
        return kInvalidArgumentsError;
    }

    if (NumberOfPossiblePIDsInArray <= 0) {
        return kInvalidArgumentsError;
    }

    if (NumberOfMatchesFound == NULL) {
        return kInvalidArgumentsError;
    }
    
    memset(ArrayOfReturnedPIDs, 0, NumberOfPossiblePIDsInArray * sizeof(pid_t));
        
    *NumberOfMatchesFound = 0;

    if (SysctlError != NULL) {
        *SysctlError = 0;
    }

    mib[0] = CTL_KERN;
    mib[1] = KERN_PROC;
    mib[2] = KERN_PROC_ALL;

    SuccessfullyGotProcessInformation = FALSE;
    
    while (SuccessfullyGotProcessInformation == FALSE) {
        error = sysctl(mib, 3, NULL, &sizeOfBufferRequired, NULL, NULL);
        if (error != 0) {
            if (SysctlError != NULL) {
                *SysctlError = errno;
            } 
            return kErrorGettingSizeOfBufferRequired;
        }
    
        BSDProcessInformationStructure =
            (struct kinfo_proc*)malloc(sizeOfBufferRequired);

        if (BSDProcessInformationStructure == NULL) {
            if (SysctlError != NULL) {
                *SysctlError = ENOMEM;
            } 

            return kUnableToAllocateMemoryForBuffer;
        }
    
        error = sysctl(mib, 3, BSDProcessInformationStructure,
                       &sizeOfBufferRequired, NULL, NULL);
    
        if (error == 0) {
            SuccessfullyGotProcessInformation = TRUE;
        } else {
            free(BSDProcessInformationStructure); 
        }
    }

    NumberOfRunningProcesses = sizeOfBufferRequired / sizeof(struct kinfo_proc);  
    for (Counter = 0; Counter < NumberOfRunningProcesses; Counter++) {
        CurrentExaminedProcessPID =
            BSDProcessInformationStructure[Counter].kp_proc.p_pid; 
    
        CurrentExaminedProcessName =
            BSDProcessInformationStructure[Counter].kp_proc.p_comm; 
        
        if ((CurrentExaminedProcessPID > 0) &&
            ((strncmp(CurrentExaminedProcessName, ProcessName, MAXCOMLEN) == 0))
           ) {	

            if ((*NumberOfMatchesFound + 1) > NumberOfPossiblePIDsInArray) {
                free(BSDProcessInformationStructure);
                return(kPIDBufferOverrunError);
            }
        
            ArrayOfReturnedPIDs[*NumberOfMatchesFound] =
                CurrentExaminedProcessPID;
            
            *NumberOfMatchesFound = *NumberOfMatchesFound + 1;
        }
    }

    free(BSDProcessInformationStructure);

    if (*NumberOfMatchesFound == 0) {
        return kCouldNotFindRequestedProcess;
    } else {
        return kSuccess;
    }
}

int
GetPIDForProcessName(const char* ProcessName)
{
    pid_t        PIDArray[1] = { 0 };
    int          Error = 0;
    unsigned int NumberOfMatches = 0;  

    Error = GetAllPIDsForProcessName(ProcessName, PIDArray, 1,
                                     &NumberOfMatches, NULL);
    
    if ((Error == 0) && (NumberOfMatches == 1)) {
        return (int)PIDArray[0];
    } else {
        return -1;
    }
}
