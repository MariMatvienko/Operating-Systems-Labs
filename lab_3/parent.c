#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <semaphore.h>

#include "proto.h"


#define STDIN_FILENO  0 // поток для ввода
#define STDOUT_FILENO 1 // поток для вывода
#define STDERR_FILENO 2 // поток для ошибок

static char SERVER_PROGRAM_NAME[] = "child";
static char SERVER_PROGRAM_PATH[] = "./child";
int main(void)
{
    char filename[256];
    int j = 0;
    char c;
    while (j < 255)
    {
        ssize_t r = read(STDIN_FILENO, &c, 1);
        if (r == 0 || c == '\n') break;
        filename[j] = c;
        j++;
    }
    filename[j] = '\0';
    //открываем файл 
    int file_fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644); 
    if (file_fd == -1)
    {
        const char msg[] = "error: cannot open file\n";
        write(STDERR_FILENO, msg, strlen(msg));
        return 1;
    }

    char shm_name[64];
    snprintf(shm_name, sizeof(shm_name), "/lab3_shm_%d", (int)getpid()); // уникальное имя для выделенной оперативной памяти

    // Просим ядро выделить именнованный объект с указанными правами, и дать нам дескриптор на него, эта память ещё не наша!
    int shm_fd = shm_open(shm_name, O_CREAT | O_RDWR | O_EXCL, 0666); // если нет - создай, если есть дай номер дескриптора, 
                                                                      // O_RDWR - и читай, и пиши, O_EXCL - создание должно быть первым и единственным, 0666 - права доступа, все могут читать и писать
    if (shm_fd == -1)
    {
        const char msg[] = "error: cannot create shared memory object\n";
        write(STDERR_FILENO, msg, strlen(msg));
        return 1;
    }

    // Изначально именнованный объект созадётся нуливого размера, поэтому просим ядро его расшить до sizeof(shm_t)
    if ((ftruncate(shm_fd, sizeof(shm_t))) == -1)
    {
        const char msg[] = "error: cannot size shared memory object\n";
        write(STDERR_FILENO, msg, strlen(msg));
        close(shm_fd);
        shm_unlink(shm_name);
        return 1;
    }

    // а теперь возьмём непосредственно указатель на эту именованную часть памяти ядра, а так же отобразим эту
    // именованную память на память нашего процесса, чтобы он мог использовать эту память как свою
    void *shm_ptr = mmap(NULL, sizeof(shm_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    
    if (shm_ptr == MAP_FAILED)
    {
        const char msg[] = "error: cannot map shared memory\n";
        write(STDERR_FILENO, msg, strlen(msg));
        close(shm_fd);
        shm_unlink(shm_name);
        return 1;
    }

    close(shm_fd); // мы уже имеем эту память как будто она наша, дескриптор не нужен
    shm_t *shared = (shm_t *)shm_ptr;

    // создание семафоров
    char line_sem_name[64], reply_sem_name[64];
    int  pid = (int)getpid();
    snprintf(line_sem_name,  sizeof(line_sem_name),  "/lab3_line_%d",  pid);
    snprintf(reply_sem_name, sizeof(reply_sem_name), "/lab3_reply_%d", pid);

    // оба стартуют с 0: в самом начале ни родитель, ни ребёнок права работать не имеют
    // ребёнок спит до первой строки, родитель спит до первого ответа
    sem_t *sem_line  = sem_open(line_sem_name,  O_CREAT | O_EXCL, 0666, 0);
    sem_t *sem_reply = sem_open(reply_sem_name, O_CREAT | O_EXCL, 0666, 0);

    if (sem_line == SEM_FAILED || sem_reply == SEM_FAILED)
    {
        const char msg[] = "error: cannot create semaphore\n";
        write(STDERR_FILENO, msg, strlen(msg));

        if (sem_line  != SEM_FAILED) { sem_close(sem_line);  sem_unlink(line_sem_name);  }
        if (sem_reply != SEM_FAILED) { sem_close(sem_reply); sem_unlink(reply_sem_name); }

        munmap(shm_ptr, sizeof(shm_t));
        shm_unlink(shm_name);
        return 1;
    }


    pid_t child = fork();
    if (child == -1)
    {
        const char msg[] = "error: fork failed\n";
        write(STDERR_FILENO, msg, strlen(msg));
        sem_close(sem_line);  sem_unlink(line_sem_name);
        sem_close(sem_reply); sem_unlink(reply_sem_name);
        munmap(shm_ptr, sizeof(shm_t));
        shm_unlink(shm_name);
        return 1;
    } else if (child == 0)
    {
        if (dup2(file_fd, STDOUT_FILENO) == -1)
        {
            const char msg[] = "error: dup2 failed\n";
            write(STDERR_FILENO, msg, strlen(msg));
            _exit(EXIT_FAILURE);
        }
        close(file_fd); 

        prctl(PR_SET_PDEATHSIG, SIGTERM);
        char *const args[] = { SERVER_PROGRAM_NAME, shm_name, line_sem_name, reply_sem_name, NULL };
        int status = execv(SERVER_PROGRAM_PATH, args);
        if (status == -1) 
        {
            const char msg[] = "error: failed to exec into new executable image\n";
            write(STDERR_FILENO, msg, strlen(msg));
            shared->to_parent.len = LEN_EOF;  // сказать родителю, что отвечать некому
            sem_post(sem_reply);              // иначе он встанет на sem_wait навсегда
            exit(EXIT_FAILURE);
        }
    } else
    {
        close(file_fd);
        char line[MAXLINE];
        int child_done = 0;
        int eof = 0;

        while (eof != 1)
        {
            int i = 0;
            while (i < MAXLINE - 1)
            {
                ssize_t r = read(STDIN_FILENO, &line[i], 1);
                if (r == -1)
                {
                    const char msg[] = "error: read failed\n";
                    write(STDERR_FILENO, msg, strlen(msg));
                    return 1;
                }
                if (r == 0) { eof = 1; break; }
                if (line[i] == '\n') { i++; break; }
                i++;
            }
            if (i == 0 && eof) break;

            shared->to_child.len = i;
            memcpy(shared->to_child.data, line, (size_t)i); // записываем строку в слот для строк

            if (sem_post(sem_line) == -1)  // прибаляем к счётчику семафора единичку и будим ребёнка для обработки
            {
                const char msg[] = "error: sem_post failed\n";
                write(STDERR_FILENO, msg, strlen(msg));
                return 1;
            }

            if (sem_wait(sem_reply) == -1) // спать пока счётчик 0, когда счётчик станет не 0, муеньшить его на 1 и пойти дальше
            {
                const char msg[] = "error: sem_wait failed\n";
                write(STDERR_FILENO, msg, strlen(msg));
                return 1;
            }

            int32_t n = shared->to_parent.len; // прочитать ответ
            if (n == LEN_EOF) { child_done = 1; break; }; // ребёнок вышел раньше
            if (n > 0)
                write(STDOUT_FILENO, shared->to_parent.data, (size_t)n);
        }

        if (!child_done)
        {
            shared->to_child.len = LEN_EOF;
            if (sem_post(sem_line) == -1)
            {
                const char msg[] = "error: sem_post failed\n";
                write(STDERR_FILENO, msg, strlen(msg));

                sem_close(sem_line);   sem_unlink(line_sem_name);
                sem_close(sem_reply);  sem_unlink(reply_sem_name);
                munmap(shm_ptr, sizeof(shm_t));
                shm_unlink(shm_name);
                return 1;
            }

            if (sem_wait(sem_reply) == -1)
            {
                const char msg[] = "error: sem_wait failed\n";
                write(STDERR_FILENO, msg, strlen(msg));

                sem_close(sem_line);   sem_unlink(line_sem_name);
                sem_close(sem_reply);  sem_unlink(reply_sem_name);
                munmap(shm_ptr, sizeof(shm_t));
                shm_unlink(shm_name);
                return 1;
            }
        }

        int status;
        if (waitpid(child, &status, 0) == -1)
        {
            const char msg[] = "error: waitpid failed\n";
            write(STDERR_FILENO, msg, strlen(msg));
            return 1;
        }
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        {
            const char msg[] = "error: child failed\n";
            write(STDERR_FILENO, msg, strlen(msg));
        }

        sem_close(sem_line);  sem_unlink(line_sem_name);
        sem_close(sem_reply); sem_unlink(reply_sem_name);
        munmap(shm_ptr, sizeof(shm_t));
        shm_unlink(shm_name);

        return 0;
    }
}