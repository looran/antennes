#!/bin/sh

trace() { echo "$ $*" >&2; "$@"; }

set -e

D="$(dirname $0)"
DL_DIR="$D/dl"
EXTRACT_DIR="$D/extract"
ANTENNES_DATASET="551d4ff3c751df55da0cd89f"

sets_count=${1-1}
offline=${OFFLINE-0}
echo "downloading last $sets_count sets"

trace mkdir -p $DL_DIR
trace mkdir -p $EXTRACT_DIR
[ $offline -eq 0 ] && trace curl "https://www.data.gouv.fr/api/1/datasets/$ANTENNES_DATASET/" > $DL_DIR/dataset.json
trace python -m json.tool $DL_DIR/dataset.json $DL_DIR/dataset_beautify.json
cat $DL_DIR/dataset.json |jq -r ".resources[] | .last_modified+\";\"+.title+\";\"+.url+\";\"+.checksum.value" > $DL_DIR/urls.txt

lastperiod=""
sets_done=0
cat $DL_DIR/urls.txt |egrep "\.zip;" |while IFS=";" read published title url sum; do
    period="$(echo $published |cut -d'-' -f1-2)"
    if [ -n "$lastperiod" -a "$lastperiod" != "$period" ]; then
        sets_done=$(($sets_done+1))
        [ $lastperiod = "2016-08" ] \
            && trace cp $EXTRACT_DIR/2016-08/20160730_DATA/* $EXTRACT_DIR/2016-08/
        [ $lastperiod = "2017-01" ] \
            && trace cp $EXTRACT_DIR/2017-02/{SUP_EXPLOITANT.txt,SUP_NATURE.txt,SUP_PROPRIETAIRE.txt,SUP_TYPE_ANTENNE.txt} $EXTRACT_DIR/2017-01
        [ $lastperiod = "2018-03" ] \
            && (cd $EXTRACT_DIR/2018-03 && 7z -y x 20180228_Export_Etalab_Data.zip >/dev/null)
        [ $sets_done -eq $sets_count ] && break
    fi
    filename=$(basename $url)
    echo "[set $(($sets_done+1))/$sets_count] '$title' for period '$period' filename '$filename'"
    [ -e $DL_DIR/$filename -o $offline -eq 1 ] \
        && echo "file already downloaded" \
        || trace wget -P $DL_DIR "$url"
    [ "$sum" != "$(trace sha1sum $DL_DIR/$filename |cut -d' ' -f1)" ] \
        && echo "error: checksum verification failed for $DL_DIR/$filename" \
        && exit 1
    mkdir -p $EXTRACT_DIR/$period
    trace 7z -aos -y x -o$EXTRACT_DIR/$period $DL_DIR/$filename >/dev/null
    lastperiod=$period
done

echo "[*] all sets downloaded to $DL_DIR and extracted in $EXTRACT_DIR"

