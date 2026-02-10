#ifndef MIC_H
#define MIC_H

#include <Arduino.h>

void setupMicrophone();
void micTask(void *parameter);
void setRecording(bool recording);

#endif
