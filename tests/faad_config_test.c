#include "neaacdec.h"
#include <stdio.h>
int main(void) {
    NeAACDecHandle d=NeAACDecOpen();
    NeAACDecConfigurationPtr c=NeAACDecGetCurrentConfiguration(d);
    printf("handle=%p config=%p object=%u rate=%lu output=%u down=%u result=%u\n",
        d,c,c?c->defObjectType:0,c?c->defSampleRate:0,c?c->outputFormat:0,
        c?c->downMatrix:0,c?NeAACDecSetConfiguration(d,c):0);
    NeAACDecClose(d); return 0;
}
