id1=`echo $1 | sed s/master-pnp-matpc-//g | sed s/-.*//g`
id2=`echo $1 | sed s/master-pnp-matpc-[0-9]*-//g | sed s/\\\\..*//g`
wget https://cdn.loc.gov/master/pnp/matpc/$id1/$id2.tif -O `basename $1 .par`.tif
