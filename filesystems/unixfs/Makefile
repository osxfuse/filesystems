DIRS = ancientfs minixfs sysvfs ufs

all:
	-for dir in $(DIRS); do (cd $$dir && make); done

clean:
	-for dir in $(DIRS); do (cd $$dir && make clean); done
