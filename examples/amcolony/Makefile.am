%-color.tif: %.par %.tif
	../../src/colorscreen/colorscreen `basename $< .par`.tif $< $@
%u.tif:
	echo $@
	wget https://cdn.loc.gov/master/pnp/matpc/`echo $@ | sed s/master-pnp-matpc-//g | sed s/-.*//g`/`echo $@ | sed s/master-pnp-matpc-[0-9]*-//g | sed s/\\\\..*//g`.tif -O $@
examples:master-pnp-matpc-00200-00279u-color.tif master-pnp-matpc-00200-00296u-color.tif master-pnp-matpc-00300-00320u-color.tif master-pnp-matpc-00400-00413u-color.tif master-pnp-matpc-00400-00418u-color.tif master-pnp-matpc-00400-00419u-color.tif
