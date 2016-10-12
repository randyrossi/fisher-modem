#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "../slow/src/BitBuffer.h"

int main(int argc, char** argv) {
  BitBuffer* b = new BitBuffer(1);
  unsigned char t = 0;
  while (t < 255) {
    b->putBit(t);
    int d = b->getBit();
    printf("%d\n", d);
    t++;
  }
}
