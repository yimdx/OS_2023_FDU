#include<kernel/console.h>
#include<kernel/init.h>
#include<aarch64/intrinsic.h>
#include<kernel/sched.h>
#include<driver/uart.h>
#define INPUT_BUF 128
struct {
    char buf[INPUT_BUF];
    usize r;  // Read index
    usize w;  // Write index
    usize e;  // Edit index
} input;
#define C(x)      ((x) - '@')  // Control-x

SpinLock lock;
Semaphore sem;
isize console_write(Inode *ip, char *buf, isize n) {
    // TODO
    inodes.unlock(ip);

    for(auto i = 0; i < n; i++){
        uart_put_char(buf[i]);
    }

    inodes.lock(ip);
    return n;
}

isize console_read(Inode *ip, char *dst, isize n) {

    // TODO
    inodes.unlock(ip);
    int total = 0;

    for(auto i = 0; i < n; i++){
        if(input.r + i == input.w){
            break;
        }
        char data = input.buf[(input.r + 1) % INPUT_BUF];

        dst[i] = data;
        input.r = (input.r + 1) % INPUT_BUF;
        total++;
        if(data == C('D')){
            break;
        }

    }

    // inodes.lock(ip);
    return total;
}

void console_intr(char (*getc)()) {
    // TODO

    char data;
    while ((data = getc()) != -1) {
        switch (data) {
            case C('D'): // Ctrl+D
                // Update write index to edit index
                if((input.e + 1)%INPUT_BUF == input.r){
                    continue;
                }
                input.w = input.e;
                uart_put_char(data);
                break;
            
            case C('U'): // Ctrl+U
                // Delete the whole line
                while (input.e != input.w) {
                    input.e--;
                    input.e = input.e % INPUT_BUF;
                    uart_put_char('\b');
                    if(input.buf[input.e] == '\n' || input.buf[input.e] == C('D')) break;
                }
                uart_put_char(' ');
                uart_put_char('\b');
                break;
            
            case '\x7f': // Backspace
                if (input.e != input.w) {
                    input.e--;
                    input.e = input.e % INPUT_BUF;
                    uart_put_char('\b');
                    uart_put_char(' ');
                    uart_put_char('\b');
                }
                break;
            
            default:
                if ((input.e - input.r) < INPUT_BUF && data != C('D')) {
                    input.buf[input.e++ % INPUT_BUF] = data;
                }
                break;
        }
    }
}
