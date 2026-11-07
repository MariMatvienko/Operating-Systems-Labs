#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <sys/mman.h>
#include <semaphore.h>

#include "proto.h"

// сервер нашей пары: читает строки из общего слота to_child, хорошие пишет в файл, а тексты ошибок
// кладёт в общий слот to_parent

int main(int argc, char *argv[])
{
    if (argc != 4)
    {
        const char msg[] = "usage: child <shm-name> <line-sem-name> <reply-sem-name>\n";
        write(STDERR_FILENO, msg, strlen(msg));
        return 1;
    }

    int shm_fd = shm_open(argv[1], O_RDWR, 0); // просто открываем переданную память
    if (shm_fd == -1)
    {
        const char msg[] = "error: cannot open shared memory object\n";
        write(STDERR_FILENO, msg, strlen(msg));
        return 1;
    }

    // отображаем ту же память на своё адресное пространство
    void *shm_ptr = mmap(NULL, sizeof(shm_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (shm_ptr == MAP_FAILED)
    {
        const char msg[] = "error: cannot map shared memory\n";
        write(STDERR_FILENO, msg, strlen(msg));
        close(shm_fd);
        return 1;
    }

    close(shm_fd); // дескриптор был нужен только чтобы объяснить mmap, куда клеить
    shm_t *shared = (shm_t *)shm_ptr;

    // открываем оба семафора
    // права и начальное значение в sem_open без O_CREAT игнорируются, но синтаксис их требует
    sem_t *sem_line  = sem_open(argv[2], 0, 0666, 0);
    sem_t *sem_reply = sem_open(argv[3], 0, 0666, 0);

    if (sem_line == SEM_FAILED || sem_reply == SEM_FAILED)
    {
        const char msg[] = "error: cannot open semaphore\n";
        write(STDERR_FILENO, msg, strlen(msg));
        munmap(shm_ptr, sizeof(shm_t));
        return 1;
    }

    char line[MAXLINE];
    int rc = 0;

    for (;;)
    {
        // ждём, пока родитель не положит строку и прибавит единичку к счётчику sem_line.
        // пока счётчик 0 - спим в очереди ядра
        if (sem_wait(sem_line) == -1)
        {
            const char msg[] = "error: sem_wait failed\n";
            write(STDERR_FILENO, msg, strlen(msg));
            rc = 1;
            break;
        }

        int32_t n = shared->to_child.len;

        if (n == LEN_EOF) // родитель сказал, что строк больше не будет
        {
            shared->to_parent.len = LEN_EOF;

            if (sem_post(sem_reply) == -1)
            {
                const char msg[] = "error: sem_post failed\n";
                write(STDERR_FILENO, msg, strlen(msg));
                rc = 1;
            }
            break;
        }

        // копируем строку из общей памяти в свою и дописываем терминатор:
        // в data никакого '\0' нет, длина передаётся отдельным полем.
        // плюс держим общий слот занятым минимум времени - дальше работаем со своей копией
        memcpy(line, shared->to_child.data, (size_t)n);
        line[n] = '\0';

        int32_t m = 0; // длина ответа родителю; 0 означает "строка хорошая, сообщения нет"

        if (isupper((unsigned char)line[0]))
        {
            write(STDOUT_FILENO, line, (size_t)n);
        }
        else
        {
            size_t msg_len = (size_t)n;
            if (msg_len > 0 && line[msg_len - 1] == '\n') msg_len--; // \n в сообщение не включаем

            const char pre[]  = "error: \"";
            const char post[] = "\" does not start with an uppercase letter\n";

            memcpy(shared->to_parent.data, pre, strlen(pre));
            m += (int32_t)strlen(pre);
            memcpy(shared->to_parent.data + m, line, msg_len);
            m += (int32_t)msg_len;
            memcpy(shared->to_parent.data + m, post, strlen(post));
            m += (int32_t)strlen(post);
        }

        // post обязан быть последней операцией, иначе родитель прочитает наполовину записанное
        shared->to_parent.len = m;

        if (sem_post(sem_reply) == -1) // заставляем родителя проснуться
        {
            const char msg[] = "error: sem_post failed\n";
            write(STDERR_FILENO, msg, strlen(msg));
            rc = 1;
            break;
        }
    }

    munmap(shm_ptr, sizeof(shm_t));
    sem_close(sem_line);
    sem_close(sem_reply);

    return rc;
}