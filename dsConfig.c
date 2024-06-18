/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2017 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
*/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "dsError.h"
#include "dsConfig.h"

#define SUCCESS 1
#define FAILURE 0




#define PLATFORM_FILE "platform.cfg"

#ifndef HAL_CONFIG_FILE
	#define HAL_CONFIG_FILE /usr/bin
#endif 

#define STRINGIFY(s) PATH(s)
#define PATH(s) #s


#define FREE(MEMORY) free(MEMORY);\
                     MEMORY = NULL;\
/*****************************************************************************
* Function/Method       : dsReadCfgFile
* Function Description  : This function reads audio configuration file for
*                         initializing audio output ports.For initializing
*                         function pointer pass has to
*                         pass as argument.
*                         ports.
* Arguments             : None
*     INPUT             : None
*     OUTPUT            : None
*     INPUT/OUTPUT      : NA
* Globals affected      : ports
* Return Value          : int Error(SUCCESS/FAILURE) Information
* Exception             : <Exception thrown if any>
* Assumptions           : configuration file is copied in same directory
*****************************************************************************/

dsError_t dsReadCfgFile(size_t index,char* portString,port_initialization_fp init)
{

    //printf("Inside dsReadCfgFile\n");	

    FILE* fptr = NULL;
    char* buff_p = NULL;
    char* valbuff_p = NULL;
    char propbuff_p[512];
    char platformFile[512];
    size_t length = 0;
    dsError_t retValue = dsERR_INVALID_PARAM; 

  sprintf(platformFile,"%s%c%s",STRINGIFY(HAL_CONFIG_FILE),47,PLATFORM_FILE);

    fptr = fopen(platformFile,"r");
    if (fptr != NULL && index >= 0 && init != NULL ) {
        while (1) {
               bzero(propbuff_p,sizeof(propbuff_p));
               buff_p = dsGetValidStringFrmCfg(fptr);
               if (buff_p != NULL) {
                   valbuff_p = strchr(buff_p,'=');
                   if (valbuff_p == NULL) {
                       FREE(buff_p);
                       continue;
                   }
                   valbuff_p++;
                   length = valbuff_p - buff_p -1;
                   memcpy(propbuff_p,buff_p,length);
                   propbuff_p[length] = '\0';
                   char* prop_p = dsGetPropertyFrmCfg(propbuff_p,index,portString);
		   printf("Property = %s\n",prop_p);
                   if(prop_p != NULL ) {
                      if (init(index,prop_p,valbuff_p) !=  dsERR_NONE) {
                          retValue = dsERR_GENERAL;
                          break;
                      }
                   }
		   else
		   {
			printf("Failed\n");
		   }
                   FREE(buff_p);
               }
               else {
                   retValue = dsERR_NONE;
                   break;
               }
        }
    }
    else
    {
	printf("Platform File Not Found\n");
    }
    fclose(fptr);
    return retValue;
}

/*****************************************************************************
* Function/Method       : dsGetValidStringFrmCfg
* Function Description  : This function is used to get the valid string from
*                         configuration file.
* Arguments             :
*     INPUT        	: dsAudioPortType_t type, int index
*     OUTPUT          	: intptr_t *handle
*     INPUT/OUTPUT     	: NA
* Globals affected      : None
* Return Value          : dsError_t  --- > Error code for Device Settings
* Exception             : <Exception thrown if any>
* Assumptions           : <assumptions if any>
*****************************************************************************/


char* dsGetValidStringFrmCfg(FILE* fptr)
{
    size_t len = 0;
    char* buff_p = NULL;
     while (1) {
            if( getline(&buff_p,&len,fptr) != -1) {
            if(buff_p[0] == '#')
            {
                FREE(buff_p);
                continue;
            }
            else
                break;
         }
         else
            {
                  buff_p = NULL;
                  break;
            }
      }
     return buff_p;
}

/*****************************************************************************
* Function/Method       : dsGetValue
* Function Description  : This function is used to get the value for given
*                         property string from configuration file.
* Arguments             :
*     INPUT        	: dsAudioPortType_t type, int index
*     OUTPUT          	: intptr_t *handle
*     INPUT/OUTPUT     	: NA
* Globals affected      : None
* Return Value          : dsError_t  --- > Error code for Device Settings
* Exception             : <Exception thrown if any>
* Assumptions           : <assumptions if any>
*****************************************************************************/


char* dsGetValue(char* property)
{
    char* buff_p;
    char* valBuff_p;
    char* retValue = NULL;
    char propBuff_p[512];  
    char platformFile[512];
    FILE* fptr;
    sprintf(platformFile,"%s%c%s",STRINGIFY(HAL_CONFIG_FILE),47,PLATFORM_FILE);
    fptr = fopen(platformFile,"r");
    if(fptr != NULL)
    {
        while(1)
        {
            buff_p = dsGetValidStringFrmCfg(fptr);
            if(buff_p != NULL)
            {
                valBuff_p = strchr(buff_p,'=');
                if(valBuff_p == NULL)
                {
                    FREE(buff_p);
                    continue;
                }
                valBuff_p++;
                int length = valBuff_p - buff_p -1;
                memset(propBuff_p,0,sizeof(propBuff_p));
                memcpy(propBuff_p,buff_p,length);
                propBuff_p[length] = '\0';
                char* str = strstr(propBuff_p,property);
                if(str != NULL)
                {
                    retValue = valBuff_p;
                    break;
                }
            }
            else
            {
                retValue = NULL;
                break;
            }
        }
    }
    fclose(fptr);
    return retValue;
}


/*****************************************************************************
* Function/Method       : dsGetPropertyFrmCfg
* Function Description  : This function retuns property name based on type of
*                         port(audio/video) and index value .
* Arguments             : *values,*prop,index
*     INPUT             : *values,*prop,index
*     OUTPUT            : int
*     INPUT/OUTPUT      : NA
* Globals affected      : audioOutputPorts
* Return Value          : int Error(SUCCESS/FAILURE) Information
* Exception             : <Exception thrown if any>
* Assumptions           : configuration file is copied in same directory
*****************************************************************************/

char* dsGetPropertyFrmCfg(char* prop,size_t index,char* portType)
{
    char lbuffer[60];
    sprintf(lbuffer,"%s.%d.",portType,index);
    char* lptr = strstr(prop,lbuffer);
    if(lptr != NULL)
    lptr = lptr + strlen(lbuffer);
    return  lptr;
}

/*****************************************************************************
* Function/Method       : dsGetIndexFrmCfg
* Function Description  : This function used to get the index of a given
*                         port.
* Arguments             : indexString
*     INPUT             : index Value
*     OUTPUT            : int
*     INPUT/OUTPUT      : NA
* Globals affected      : None
* Return Value          : size_t Error(SUCCESS/FAILURE) Information
* Exception             : <Exception thrown if any>
* Assumptions           : configuration file is copied in same directory
*****************************************************************************/

size_t dsGetIndexFrmCfg(char* indexString)
{
    char* buff_p = NULL;
    char* valbuff_p = NULL; 
    char propbuff_p[512];
    char platformFile[512];
    size_t index = 0;
    FILE* fptr;
	
    sprintf(platformFile,"%s%c%s",STRINGIFY(HAL_CONFIG_FILE),47,PLATFORM_FILE);
    fptr = fopen(platformFile,"r");
    if(fptr != NULL)
    {
        while(1)
        {
            buff_p = dsGetValidStringFrmCfg(fptr);
            memset(propbuff_p,0,sizeof(propbuff_p));
            if(buff_p != NULL)
            {
                    /* Checks whether string is having = sign or not ,if not ignore it */
                    valbuff_p = strchr(buff_p,'=');
                    if(valbuff_p == NULL)
                    {
                        FREE(buff_p);
                        continue;
                    }
                    valbuff_p++;
                    int length = valbuff_p - buff_p -1;
                    memcpy(propbuff_p,buff_p,length);
                    propbuff_p[length] = '\0';
                    char* lptr = strstr(propbuff_p,indexString);
                    if(lptr != NULL)
                    {
                        lptr = lptr + strlen(indexString);
                        lptr++;
                        if(strcmp(lptr,"index") == 0)
                        {
                            sscanf(valbuff_p,"%d",&index);
                            FREE(buff_p);
                            break;
                        }
                    }
                    FREE(buff_p);
            }
            else
                  break;
        }
    }
    fclose(fptr);
    return index;
}

