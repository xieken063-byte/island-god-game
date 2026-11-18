# 主机端测试：不需要设备和 ESP-IDF，只要 gcc。
CFLAGS = -std=gnu11 -O1 -g -Wall -Wextra -fsanitize=address,undefined -I../main
SRC = ../main/sim.c ../main/sim_world.c ../main/render.c ../main/game.c ../main/sfx.c ../main/font_data.c host_main.c

host_test: $(SRC) $(wildcard ../main/*.h)
	$(CC) $(CFLAGS) $(SRC) -o $@

test: host_test
	./host_test save
	./host_test away
	./host_test tree
	./host_test audio
	./host_test powers
	./host_test fuzz
	./host_test soak

shots: host_test
	mkdir -p out && ./host_test shots out

clean:
	rm -rf host_test out
