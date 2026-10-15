#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h> 
#include <stdbool.h>

typedef struct {
	uint8_t rank; // номинал от 0 до 12
	uint8_t suit; // масть от 0 до 3
} Card;

void out_str(const char *s)
{
	size_t len = 0;
	while (s[len] != '\0')
		len++;
	write(STDOUT_FILENO, s, len);
}

void out_uint(int x)
{
	char buf[21];
	int pos = 20;

	while (x > 0) 
    {
		buf[pos--] = (char)('0' + x % 10);
		x /= 10;
	}

	write(STDOUT_FILENO, buf + pos + 1, 20 - pos);
}

void deck_shuffle(Card *deck, unsigned *seed) // перемешивание колоды
{
	for (int i = 51; i > 0; i--) {
		int j = rand_r(seed) % (i + 1);
		Card tmp = deck[i];
		deck[i] = deck[j];
		deck[j] = tmp;
	}
}

bool cards_match(const Card *a, const Card *b)
{
	return a->rank == b->rank || a->suit == b->suit;
}

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        out_str("Используйте: ");
		out_str(argv[0]);
		out_str(" <количество раундов>\n");
		return 1;
    }

    long n = strtol(argv[1], NULL, 10);

    if (n < 1) 
    {
		out_str("Число раундов должно быть >= 1\n");
		return 1;
	}

	uint64_t N = (uint64_t)n;

    struct timespec start, stop;  // таймер
	clock_gettime(CLOCK_MONOTONIC, &start);

    uint64_t result = 0;

	Card deck[52];
	int number = 0;
	for (int i = 0; i < 13; i++)
    {
		for (int j = 0; j < 4; j++) 
        {
			deck[number].rank = i;
			deck[number].suit = j;
			number++;
		}
    }
	unsigned seed = 1;

	for (int r = 0; r < n; r++) 
    {
		deck_shuffle(deck, &seed);
		if (cards_match(&deck[0], &deck[1]))
			result++;
	}
 
    out_str("Из ");
    out_uint(n);
    out_str(" перемешивания(ий) колоды ");
    out_uint(result);
    out_str(" раз первые две карты оказались одинаковыми по масти или номиналу.\n");

    out_str("Теоретическая вероятность этого равна 390/1326 ~ 0,294.\n");

    clock_gettime(CLOCK_MONOTONIC, &stop);
	long long delta_ms = (long long)(stop.tv_sec - start.tv_sec) * 1000 + (long long)(stop.tv_nsec - start.tv_nsec) / 1000000;

	out_str("Время выполнения: ");
	out_uint(delta_ms);
	out_str(" милисекунд(ы)\n");
}
