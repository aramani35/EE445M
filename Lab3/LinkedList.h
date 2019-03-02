#include <stdint.h>


void linkThread(TCBtype *linkPT, TCBtype *threadPT, int threads);

void unlinkThread(TCBtype *threadPT, int threads);

void addToList(TCBtype *threadPT, TCBtype **startPT, TCBtype **tailPT);

void removeFromList(TCBtype *threadPT, TCBtype **startPT, TCBtype **tailPT);
