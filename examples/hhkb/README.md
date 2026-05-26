# SPDX-License-Identifier: MIT
#
# nRF52833 HHKB keyboard configuration example using zmk-esb-endpoint module
#
# This is an example showing how to set up an HHKB-layout keyboard
# with the ESB endpoint module. Copy/adapt these files to your ZMK
# firmware project (e.g. zmk-config/).
#
# How to use:
#   1. Create a ZMK firmware project
#   2. Add this module to west.yml:
#      - name: zmk-esb-endpoint
#        remote: your-remote
#        revision: main
#        path: modules/esb
#   3. Copy this overlay and keymap to your keyboard's config/
#   4. Include the conf settings in your .conf file
#
# For a complete guide, see:
#   https://github.com/efogdev/zmk-esb-endpoint
