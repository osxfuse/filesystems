/*
 *  command_main.c
 *  AccessibilityFS
 *
 *  Created by Dave MacLachlan on 2007/12/13.
 *
 */

// ================================================================
// Copyright (C) 2008 Google Inc.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//      http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
// ================================================================

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>

typedef struct {
  const char* command;
  const char* axcommand;
} CommandMap;

const CommandMap kMap [] = { 
  { "cancel", "AXCancel" },
  { "confirm", "AXConfirm" },
  { "decrement", "AXDecrement" },
  { "increment", "AXIncrement" },
  { "press", "AXPress" },
  { "raise", "AXRaise" },
  { "showmenu", "AXShowMenu" },
};

void showDocumentation() {
  fprintf(stderr, "ax_command <command> <object>\nwhere command is one of:\n");
  for (int i = 0; i < sizeof(kMap) / sizeof(CommandMap); ++i) {
    fprintf(stderr, "%s\n", kMap[i].command);
  }
}

int main(int argc, char** argv) {
  if (argc != 3) {
    showDocumentation();
    return -1;
  }
  
  const char *command = NULL;
  for (int i = 0; i < sizeof(kMap) / sizeof(CommandMap); ++i) {
    if (strcasecmp(argv[1], kMap[i].command) == 0) {
      command = kMap[i].axcommand;
      break;
    }
  }  
  if (!command) {
    showDocumentation();
    return -1;
  }
  
  int file = open(argv[2], O_WRONLY);
  if (file == -1) {
    showDocumentation();
    fprintf(stderr, "Unable to open file %s (%d)\n", argv[2], errno);
    return errno;
  }
  ssize_t size = write(file, command, strlen(command));
  if (size == -1) {
    showDocumentation();
    fprintf(stderr, "Unable to perform %s (%d)\n", command, errno);
    close(file);
    return errno;
  }
  
  return close(file);
    
    
}