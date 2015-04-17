ORACLE_HOME = /oracle/orahome
ORA_H = -I$(ORACLE_HOME)/rdbms/public
ORA_LIB_DIR = -L$(ORACLE_HOME)/lib
ORA_LIB = -lclntsh
UTL_DIR = ../../utils
UTL_H = -I../..

smsboxclient: ./obj/main.o ./obj/dbpool.o ./obj/log.o ./obj/config.o
	g++ -o ../smsboxclient ./obj/main.o ./obj/dbpool.o ./obj/config.o ./obj/log.o $(ORA_LIB_DIR) $(ORA_LIB) -lcurl
./obj/main.o: main.cpp
	g++  $(UTL_H) $(ORA_H) -c main.cpp -o ./obj/main.o
./obj/dbpool.o: $(UTL_DIR)/dbpool/dbpool.cpp
	g++ -c $(UTL_DIR)/dbpool/dbpool.cpp $(UTL_H) $(ORA_H) -o ./obj/dbpool.o
./obj/config.o: $(UTL_DIR)/config/config.cpp
	g++ -c $(UTL_DIR)/config/config.cpp -o ./obj/config.o
./obj/log.o: $(UTL_DIR)/log/log.cpp
	g++ -c $(UTL_DIR)/log/log.cpp -o ./obj/log.o

install:
	cp -f ../smsboxclient /usr/local/bin

clean:
	rm -f ./obj/* ../*.so