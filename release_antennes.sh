#!/bin/bash

ANT_DIR="$(dirname $0)"
TMP_DIR="/tmp/antennes_release"

trace() { echo "$ $*" >&2; "$@"; }

do_release() {
	host="$1"
	host_dir="$2"
	extract_dir="$3"
	period="$(basename $extract_dir)"
	latest_period="$(ls $ANT_DIR/extract/ |tail -n1)"
	period_kml_prop=anfr_${period}_proprietaires.kml
	period_kml_dept=anfr_${period}_departements.kml
	period_kml_dept_light=anfr_${period}_departements_light.kml
	period_stats=anfr_${period}_stats.txt
	period_proprietaire=anfr_${period}_proprietaire
	period_departement=anfr_${period}_departement
	period_systeme=anfr_${period}_systeme

	echo "=== release for $period ==="

	trace rm -rf $TMP_DIR
	trace mkdir -p $TMP_DIR

	echo [+] generating KML files and statistics
	mkdir $TMP_DIR/output $TMP_DIR/bands
	trace $ANT_DIR/antennes -b $TMP_DIR/bands -k $TMP_DIR/output -s $extract_dir > $TMP_DIR/output/stats.txt
	trace ls -lh $TMP_DIR/output

	echo [+] renaming output files to prepend the period name
	trace mkdir $TMP_DIR/release
	trace cp $TMP_DIR/output/anfr_proprietaires.kml $TMP_DIR/release/$period_kml_prop
	trace cp $TMP_DIR/output/anfr_departements.kml $TMP_DIR/release/$period_kml_dept
	trace cp $TMP_DIR/output/anfr_departements_light.kml $TMP_DIR/release/$period_kml_dept_light
	trace cp $TMP_DIR/output/stats.txt $TMP_DIR/release/$period_stats
	trace mkdir $TMP_DIR/release/$period_proprietaire
	for f in $TMP_DIR/output/anfr_proprietaire/*; do
		cp $f "$TMP_DIR/release/$period_proprietaire/${period_proprietaire}_$(basename $f |cut -c19-)"
	done
	trace mkdir $TMP_DIR/release/$period_departement
	for f in $TMP_DIR/output/anfr_departement/*; do
		cp $f "$TMP_DIR/release/$period_departement/${period_departement}_$(basename $f |cut -c18-)"
	done
	trace mkdir $TMP_DIR/release/$period_systeme
	for f in $TMP_DIR/output/anfr_systeme/*; do
		cp "$f" "$TMP_DIR/release/$period_systeme/${period_systeme}_$(basename "$f" |cut -c14-)"
	done

	echo [+] copying release files to web server
	trace ssh $host "mkdir -p $host_dir/split"
	trace ssh $host "mkdir -p $host_dir/bands/$period"
	trace scp $TMP_DIR/release/$period_stats $host:$host_dir
	trace scp -r $TMP_DIR/release/anfr_${period}_proprietaire $host:$host_dir/split
	trace scp -r $TMP_DIR/release/anfr_${period}_departement $host:$host_dir/split
	trace scp -r $TMP_DIR/release/anfr_${period}_systeme $host:$host_dir/split
	trace scp $TMP_DIR/bands/* $host:$host_dir/bands/$period
	trace scp $TMP_DIR/release/$period_kml_prop $host:$host_dir
	trace scp $TMP_DIR/release/$period_kml_dept $host:$host_dir
	trace scp $TMP_DIR/release/$period_kml_dept_light $host:$host_dir

	if [ $latest_period = $period ]; then
		echo [+] link release files as latest
		latest_prefix=anfr_0000-latest
		trace ssh $host "rm -f $host_dir/${latest_prefix}_proprietaires.kml && ln -s $period_kml_prop $host_dir/${latest_prefix}_proprietaires.kml"
		trace ssh $host "rm -f $host_dir/${latest_prefix}_departements.kml && ln -s $period_kml_dept $host_dir/${latest_prefix}_departements.kml"
		trace ssh $host "rm -f $host_dir/${latest_prefix}_departements_light.kml && ln -s $period_kml_dept_light $host_dir/${latest_prefix}_departements_light.kml"
		trace ssh $host "rm -f $host_dir/${latest_prefix}_stats.txt && ln -s $period_stats $host_dir/${latest_prefix}_stats.txt"
		trace ssh $host "rm -f $host_dir/split/${latest_prefix}_proprietaire && ln -s $period_proprietaire $host_dir/split/${latest_prefix}_proprietaire"
		trace ssh $host "rm -f $host_dir/split/${latest_prefix}_departement && ln -s $period_departement $host_dir/split/${latest_prefix}_departement"
		trace ssh $host "rm -f $host_dir/split/${latest_prefix}_systeme && ln -s $period_systeme $host_dir/split/${latest_prefix}_systeme"
	fi

	echo [*] done, released files to $host:$host_dir
}

set -e

[ $# -lt 2 ] && echo "usage: $0 <host> <host_dir> [<periods_count> [<periods_skip>]]" && exit 1
host=$1
host_dir=$2
periods_count=${3-1}
periods_skip=${4-0}
while read -u 3 extract_dir; do
	if [ $periods_skip -gt 0 ]; then
		periods_skip=$(($periods_skip-1))
		continue
	fi
	do_release $host $host_dir $extract_dir
done 3< <(ls -r1d $ANT_DIR/extract/* |head -n$periods_count)

cat > $TMP_DIR/README.txt <<-_EOF
KML exports and statistics on french antennas based on ANFR data from 2015 to now
* anfr_YYYY-MM_departements.kml [~200MB] KML file containing all _supports_ organised by _departement_
* anfr_YYYY-MM_departements_light.kml [~30MB] KML file containing all _supports_ organised by _departement_ and with no description
* anfr_YYYY-MM_proprietaires.kml [~200MB] KML file containing all _supports_ organised by _proprietaire_
* anfr_YYYY-MM_stats.txt [~2KB] statistics for the period

In split/ you can find the splited KML files for each period:
* split/anfr_YYYY-MM_departement/anfr_YYYY-MM_departement_<dept-id>.kml [<10MB] a KML file with _supports_ for a single _departement_
* split/anfr_YYYY-MM_proprietaire/anfr_YYYY-MM_proprietaire_<prop-id>_<prop-name>.kml [<30MB] a KML file with _supports_ owned by a single _proprietaire_
* split/anfr_YYYY-MM_systeme/anfr_YYYY-MM_systeme_<sys-id>_<prop-name>.kml [12KB-200MB] a KML file with _supports_ that host a given system, organized by _departement_

In bands/ you can find frequency band usage statistics in CSV files:
* bands/YYYY-MM/<exploitant>_bands.csv [<5KB] all bands used by an exploitant together with emetteur count and systemes count per band

files generated by https://github.com/looran/antennes on $(date)
_EOF
trace scp $TMP_DIR/README.txt $host:$host_dir

echo [*] all done
