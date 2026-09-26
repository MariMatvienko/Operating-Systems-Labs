#include <stdint.h>
#include <stdbool.h>

#include <unistd.h>
#include <string.h>
#include <fcntl.h> 
#include <sys/wait.h>
#include <stdlib.h>
#include <ctype.h> 

// будем считать, что на вход не будут подаваться строки длины больше 4096 
int read_line(char *buf, size_t size) // читает строку размера не более size из STDIN
{
    size_t n = 0;
    while (n < size - 1)
    {
        ssize_t r = read(STDIN_FILENO, &buf[n], 1);
        if (r == -1) return -1;
        if (r == 0) break;
        if (buf[n] == '\n') {n++; break;}
        n++;
    }
    buf[n] = '\0';
    return (int)n;
}

int main(void)
{
    char line[4096];
    int n;
    // читаем и обрабатываем строки из ввода пока они не закончатся
    while ((n = read_line(line, sizeof(line))) > 0) { // читаем строку из ввода
        if (isupper((unsigned char)line[0])) { // проверяем первый символ строки, если он заглавный - пишем в стандартный поток вывода
            write(STDOUT_FILENO, line, n);
        } else { // если нет, посылаем ошибки в STDERR
            size_t len = (size_t)n;
            if (len > 0 && line[len - 1] == '\n') len--; // \n в сообщение не включаем
            const char pre[]  = "error: \"";
            const char post[] = "\" does not start with an uppercase letter\n";
            write(STDERR_FILENO, pre, strlen(pre));
            write(STDERR_FILENO, line, len);
            write(STDERR_FILENO, post, strlen(post));
        }
    }
    return 0;
}