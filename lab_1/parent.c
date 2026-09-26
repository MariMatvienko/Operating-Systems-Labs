#include <stdint.h>
#include <stdbool.h>

#include <unistd.h>
#include <string.h>
#include <fcntl.h> 
#include <sys/wait.h>
#include <stdlib.h>
#include <ctype.h>

#define STDIN_FILENO  0 // поток для ввода
#define STDOUT_FILENO 1 // поток для вывода
#define STDERR_FILENO 2 // поток для ошибок

// SERVER_PROGRAM_NAME и SERVER_PROGRAM_PATH видны только в этой единице трансляции
static char SERVER_PROGRAM_NAME[] = "child";
static char SERVER_PROGRAM_PATH[] = "./child"; // так как файл второй программы лежит там же, где первая, достаточно добавить к имени ./
int main(void)
{
    // читаем по символьно ввод пока не встретим \n или конец ввода - это будет первая строка, название файла
    char filename[256];
    int j = 0;
    char c;
    while (j < 255)
    {
        ssize_t r = read(STDIN_FILENO, &c, 1);
        if (r == 0 || c == '\n') break; // встретили конец ввода или перевод строки
        filename[j] = c;
        j++;
    }
    filename[j] = '\0';
    //открываем файл 
    int file_fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644); // открой только на запись, если нет - создай, если есть - сделай пустым
    if (file_fd == -1) // если не удалось открыть файл
    {
        const char msg[] = "error: cannot open file\n";
        write(STDERR_FILENO, msg, strlen(msg));
        return 1;
    }
    int pipe_data[2]; // parent -> child : строки для проверки
    int pipe_error[2]; // child  -> parent: ошибки
    // pipe_data[1] - вход в трубу для записи, сюда подаются данные
    // pipe_data[0] - выход из трубы для чтения, отсюда данные только читаются
    if (pipe(pipe_data) == -1 || pipe(pipe_error) == -1) // создание труб из pipe_data и pipe_error и проверка на успех
    {
        const char msg[] = "error: cannot create pipe\n";
        write(STDERR_FILENO, msg, strlen(msg));
        return 1;
    }

    pid_t child = fork();
    // родитель видит в child номер процесса, являющегося его ребёнком
    // ребёнок видит в child ноль 
    if (child == -1) // не удалось создать процесс
    {
        const char msg[] = "error: fork failed\n";
        write(STDERR_FILENO, msg, strlen(msg));
        return 1;
    } else if (child == 0) // если я ребёнок, child = 0 и мы идём сюда
    {
        // так как fork создаёт полную копию процесса, копируются и все дескрипторы, поэтому не нужные трубы нужно закрыть тут
        close(pipe_data[1]); // закрываем вход трубы для подачи строк, ребёнок их только читаем
        close(pipe_error[0]); // закрываем выход трубы для передачи ошибок, ребёнок ошибки только отправляет

        // настраиваем перенаправление стандартных потоков ввода-вывода
        if (dup2(pipe_data[0], STDIN_FILENO) == -1) // меняю стандартный поток для ввода данных на трубу с данными от родителя
        {
            const char msg[] = "error: dup2 failed\n";
            write(STDERR_FILENO, msg, strlen(msg));
            exit(EXIT_FAILURE);
        }
        if (dup2(file_fd, STDOUT_FILENO) == -1) // меняю стандартный поток для вывода данных на указанный файл
        {
            const char msg[] = "error: dup2 failed\n";
            write(STDERR_FILENO, msg, strlen(msg));
            exit(EXIT_FAILURE);
        }
        if (dup2(pipe_error[1], STDERR_FILENO) == -1) // меняю стандартный поток ошибок на вход трубы для передачи ошибок
        {
            const char msg[] = "error: dup2 failed\n";
            write(STDERR_FILENO, msg, strlen(msg));
            exit(EXIT_FAILURE);
        }

        close(pipe_data[0]); // так как мы уже перенаправили трубу на стандартный поток, то закрываем исходный дескриптор
        close(file_fd);
        close(pipe_error[1]);
        char *const args[] = {SERVER_PROGRAM_NAME, NULL};
        int status = execv(SERVER_PROGRAM_PATH, args); // процесс, являющийся ребёнком, забывает весь код, что знает до этой команды, и начинает выполнять код, написанный в SERVER_PROGRAM_PATH
        if (status == -1) 
        {
			const char msg[] = "error: failed to exec into new executable image\n";
			write(STDERR_FILENO, msg, strlen(msg));
			exit(EXIT_FAILURE);
		}
    } else // если я родитель, child != нулю и мы попадаем сюда
    {
        close(pipe_data[0]); // закрываем неиспользующиеся родителем дескрипторы
        close(file_fd);
        close(pipe_error[1]);

        char line[4096]; // будем считать, что нам на вход подаются строки длины 4096 или меньше
        int eof = 0;
        while (eof != 1) // читаем строки до тех пор, пока пользователь не закончит ввод
        {
            int i = 0;
            while (i < 4095) // начинаем читать ввод по одному символу
            {
                ssize_t r = read(STDIN_FILENO, &line[i], 1);
                if (r == -1) 
                {
                    const char msg[] = "error: read failed\n";
                    write(STDERR_FILENO, msg, strlen(msg));
                    return 1;
                }
                if (r == 0) { eof = 1; break; } // помечаем что ввод закончился
                if (line[i] == '\n') {i++; break;} // если дошли до конца строки, необходимо выйти и её обработать
                i++;
            }
            if (i == 0 && eof) break; // если и строка не введена и ввод закончился, значит отправлять больше нечего
            write(pipe_data[1], line, i);
        }
        close(pipe_data[1]); // в трубу уже всё было записано, поэтому закрываем её

        char buf[4096];
        ssize_t r;
        while ((r = read(pipe_error[0], buf, sizeof(buf))) > 0) 
        {
            write(STDOUT_FILENO, buf, r); // выводим ошибки на экран
        }
        close(pipe_error[0]);  

        wait(NULL); // ожидание, когда завершится работа дитя    
    }
}