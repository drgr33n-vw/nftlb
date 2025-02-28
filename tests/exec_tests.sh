#!/bin/bash

ARG="$1"
ARG2="$2"
NFTBIN="nft"
NFTLBIN="../src/nftlb"
NFTLB_ARGS=""
APISERVER=0
APISRV_PORT=5555
APISRV_KEY="hola"
CURL=`which curl`

TESTS=""
APPLY_REPORTS=0

if [ "${ARG}" == "-s" -a -e "$CURL" ]; then
	APISERVER=1
elif [ "${ARG}" == "-apply-reports" ]; then
	APPLY_REPORTS=1
	TESTS="${ARG2}"
elif [[ -d ${ARG} ]]; then
	TESTS="${ARG}"
elif [ "${ARG}" == "" ]; then
	TESTS="*/"
fi

if [ "$TESTS" == "" -a "${ARG2}" == "" ]; then
	TESTS="*/"
fi

echo "" > /var/log/syslog

if [ $APISERVER -eq 1 ]; then
	$NFTLBIN $NFTLB_ARGS -d -k "$APISRV_KEY" -l 7 > /dev/null
fi

echo "-- Executing configuration tests"

for test in `ls -d ${TESTS}`; do
	if [[ ! ${test} =~ ^..._ ]]; then
		continue;
	fi

	echo -n "Executing test: ${test}... "

	inputfile="${test}/input.json"
	outputfile="${test}/output.nft"
	reportfile="${test}/report-output.nft"

	if [ $APISERVER -eq 1 ]; then
		$CURL -H "Expect:" -H "Key: $APISRV_KEY" -X DELETE http://localhost:$APISRV_PORT/farms
		$CURL -H "Expect:" -H "Key: $APISRV_KEY" -X POST http://localhost:$APISRV_PORT/farms -d "@${inputfile}"
		statusexec=$?
	else
		$NFTLBIN $NFTLB_ARGS -e -l 7 -c ${inputfile}
		statusexec=$?
	fi

	if [ $statusexec -ne 0 ]; then
		echo -e "\e[31mNFT EXEC ERROR\e[0m"
		continue;
	fi

	$NFTBIN list ruleset > ${reportfile}

	if [ ! -f ${outputfile} ]; then
		echo "Dump file doesn't exist"
		continue;
	fi

	diff -Nru ${outputfile} ${reportfile}
	statusnft=$?

	if [ $statusnft -eq 0 ]; then
		echo -e "\e[32mOK\e[0m"
		rm -f ${reportfile}
	else
		echo -e "\e[31mNFT DUMP ERROR\e[0m"
		if [ $APPLY_REPORTS -eq 1 ]; then
			cat ${reportfile} > ${outputfile}
			echo -e "APPLIED"
		fi
	fi
done

if [ $APISERVER -eq 1 ]; then
	kill `pidof nftlb`
fi

if [ "`grep 'nft command error' /var/log/syslog`" != "" ]; then
	echo -e "\e[33m* command errors found, please check syslog\e[0m"
fi
