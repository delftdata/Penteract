#!/bin/bash
set -e

LOCAL_BUILD=0
if [[ "${1:-}" == "--local" ]]; then
    LOCAL_BUILD=1
    shift
fi

TEST_TARGET="${TEST_TARGET:-consistency_singleregion_test}"

PROTOCOLS=(
    "detock:DDR:COUNTERLESS:AllProtocols/ConsistencySingleRegionTest.RunsConsistentWorkload/Detock"
    "ddr_only:DDR:COUNTERLESS:AllProtocols/ConsistencySingleRegionTest.RunsConsistentWorkload/DDROnly"
    "slog:DDR:COUNTERLESS:AllProtocols/ConsistencySingleRegionTest.RunsConsistentWorkload/Slog"
    "calvin:DDR:COUNTERLESS:AllProtocols/ConsistencySingleRegionTest.RunsConsistentWorkload/Calvin"
    "janus:DDR:COUNTERLESS:AllProtocols/ConsistencySingleRegionTest.RunsConsistentWorkload/Janus"
)

OUTPUT_DIR="${OUTPUT_DIR:-acid_results}"
mkdir -p "${OUTPUT_DIR}"
RESULTS_CSV="${RESULTS_CSV:-${OUTPUT_DIR}/singleregion_consistency_results.csv}"
LOG_DIR="${LOG_DIR:-${OUTPUT_DIR}/singleregion_consistency_logs}"

mkdir -p "${LOG_DIR}"
echo "scope,lock_manager,remaster_protocol,test_filter,image_tag,build_ok,run_exit,total_tests,passed_tests,failed_tests,elapsed_ms,log_file" > "${RESULTS_CSV}"

NPROC=$(nproc 2>/dev/null || sysctl -n hw.logicalcpu)
OVERALL_EXIT=0

for ENTRY in "${PROTOCOLS[@]}"; do
    IFS=":" read -r SCOPE LM REMASTER FILTER <<< "$ENTRY"

    echo "========================================"
    echo "Single-region consistency — $SCOPE (LM=$LM, REMASTER=$REMASTER)"
    echo "========================================"

    TAG_LM=$(echo "$LM" | tr '[:upper:]' '[:lower:]')
    TAG_RM=$(echo "$REMASTER" | tr '[:upper:]' '[:lower:]')
    TAG="${TAG_LM}-${TAG_RM}-${TEST_TARGET}"
    LOG_FILE="${LOG_DIR}/${SCOPE}.log"
    BUILD_OK=1

    if [ "${LOCAL_BUILD}" -eq 0 ]; then
        if ! docker build \
            -t detock-test:${TAG} \
            -f scripts/acid/Dockerfile.test \
            --build-arg LOCK_MANAGER="${LM}" \
            --build-arg REMASTER_PROTOCOL="${REMASTER}" \
            --build-arg TEST_TARGET="${TEST_TARGET}" \
            .; then
            BUILD_OK=0
            OVERALL_EXIT=1
            echo "${SCOPE},${LM},${REMASTER},${FILTER},${TAG},${BUILD_OK},-1,0,0,0,0,${LOG_FILE}" >> "${RESULTS_CSV}"
            continue
        fi
    elif [ "${PREBUILT:-0}" -eq 0 ]; then
        # Standalone --local run: build this target now.
        SYSTEM_PROTOC="$(command -v protoc || true)"
        PROTOC_FLAG=""
        [[ -n "${SYSTEM_PROTOC}" ]] && PROTOC_FLAG="-DProtobuf_PROTOC_EXECUTABLE=${SYSTEM_PROTOC}"
        mkdir -p build
        cmake -S . -B build -DBUILD_SLOG_TESTS=ON -DLOCK_MANAGER="${LM}" -DREMASTER_PROTOCOL="${REMASTER}" -DCMAKE_BUILD_TYPE=release ${PROTOC_FLAG}
        if ! make -C build -j"${NPROC}" "${TEST_TARGET}"; then
            BUILD_OK=0
            OVERALL_EXIT=1
            echo "${SCOPE},${LM},${REMASTER},${FILTER},${TAG},${BUILD_OK},-1,0,0,0,0,${LOG_FILE}" >> "${RESULTS_CSV}"
            continue
        fi
    fi

    RUN_EXIT=0
    if [ "${LOCAL_BUILD}" -eq 1 ]; then
        if ! ./build/test/${TEST_TARGET} --gtest_filter="${FILTER}" 2>&1 | tee "${LOG_FILE}"; then
            RUN_EXIT=${PIPESTATUS[0]}; OVERALL_EXIT=1
        fi
    else
        if ! docker run --rm -e GTEST_FILTER="${FILTER}" detock-test:${TAG} 2>&1 | tee "${LOG_FILE}"; then
            RUN_EXIT=${PIPESTATUS[0]}; OVERALL_EXIT=1
        fi
    fi

    TOTAL_TESTS=$(awk '/^\[==========\] [0-9]+ tests? from [0-9]+ test suites? ran\./ {print $2}' "${LOG_FILE}" | tail -n1)
    PASSED_TESTS=$(awk '/^\[  PASSED  \] [0-9]+ tests?\./ {print $4}' "${LOG_FILE}" | tail -n1)
    FAILED_TESTS=$(awk '/^\[  FAILED  \] [0-9]+ tests?, listed below:/ {print $4}' "${LOG_FILE}" | tail -n1)
    ELAPSED_MS=$(awk -F'[()]' '/^\[==========\] [0-9]+ tests? from [0-9]+ test suites? ran\./ {gsub(/ ms total/, "", $2); print $2}' "${LOG_FILE}" | tail -n1)

    echo "${SCOPE},${LM},${REMASTER},${FILTER},${TAG},${BUILD_OK},${RUN_EXIT},${TOTAL_TESTS:-0},${PASSED_TESTS:-0},${FAILED_TESTS:-0},${ELAPSED_MS:-0},${LOG_FILE}" >> "${RESULTS_CSV}"
done

[ "${OVERALL_EXIT}" -ne 0 ] && echo "Single-region consistency finished with failures." && exit "${OVERALL_EXIT}"
echo "Single-region consistency complete. Results: ${RESULTS_CSV}"
