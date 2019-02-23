#include <stdint.h>


void unlink(TCBtype *threadPT, int threads);

void addToList(TCBtype *threadPT, TCBtype **startPT, TCBtype **tailPT);

void RemoveFromList(TCBtype *threadPT, TCBtype **startPT, TCBtype **tailPT);
