cp ../AOA/test-* .
runs=10
namet=P`hostname | cut -c11`-`date +%d-%m-%y@@%H-%M`
./test.sh $1-$namet-LL5K $runs 5000
./testh.sh $1-$namet-HASH $runs
./test.sh $1-$namet-LL128 $runs 128

echo "Output"
./parseData.sh measure-$1-$namet-LL5K- $runs > out1
./parseData.sh measure-$1-$namet-LL128- $runs > out2
./parseData.sh measure-$1-$namet-HASH- $runs > out3
paste out1 out2 out3
echo "WithRC"
./parseData.sh measure-$1-$namet-LL5K-  $runs  RC > out1
./parseData.sh measure-$1-$namet-LL128- $runs  RC > out2
./parseData.sh measure-$1-$namet-HASH-  $runs  RC > out3
paste out1 out2 out3


