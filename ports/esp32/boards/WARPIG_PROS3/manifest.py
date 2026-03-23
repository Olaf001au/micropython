include("$(PORT_DIR)/boards/manifest.py")

# WarpPig board helper — frozen into firmware
freeze("$(BOARD_DIR)/modules")
