rm build/tests/threads/$1.output
make build/tests/threads/$1.result
cd build
pintos run $1
cd ..

