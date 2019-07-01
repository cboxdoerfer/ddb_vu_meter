#!/bin/sh
/bin/mkdir -pv ${HOME}/.local/lib/deadbeef
## GTK2 version
if [ -f ./gtk2/ddb_vis_vu_meter_GTK2.so ]; then
	/usr/bin/install -v -c -m 644 ./gtk2/ddb_vis_vu_meter_GTK2.so ${HOME}/.local/lib/deadbeef/
else
	/usr/bin/install -v -c -m 644 ./ddb_vis_vu_meter_GTK2.so ${HOME}/.local/lib/deadbeef/
fi
## GTK3 version
if [ -f ./gtk3/ddb_vis_vu_meter_GTK3.so ]; then
	/usr/bin/install -v -c -m 644 ./gtk3/ddb_vis_vu_meter_GTK3.so ${HOME}/.local/lib/deadbeef/
else
	/usr/bin/install -v -c -m 644 ./ddb_vis_vu_meter_GTK3.so ${HOME}/.local/lib/deadbeef/
fi

# retro stereo VU meter
/usr/bin/install -v -c -m 644 ./vumeterStereo.png ${HOME}/.local/lib/deadbeef/

CHECK_PATHS="/usr/local/lib/deadbeef /usr/lib/deadbeef"
for path in $CHECK_PATHS; do
	if [ -d $path ]; then
		if [ -f $path/ddb_vis_vu_meter_GTK2.so -o -f $path/ddb_vis_vu_meter_GTK3.so ]; then
			echo "Warning: Some version of the vu meter plugin is present in $path, you should remove it to avoid conflicts!"
		fi
	fi
done
