board_runner_args(jlink "--device=nRF54L15_M33" "--speed=1000")

include(${ZEPHYR_BASE}/boards/common/nrfutil.board.cmake)
include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
