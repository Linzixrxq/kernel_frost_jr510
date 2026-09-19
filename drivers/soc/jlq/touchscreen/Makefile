#
# (C) COPYRIGHT ARM Limited. All rights reserved.
#
# This program is free software and is provided to you under the terms of the
# GNU General Public License version 2 as published by the Free Software
# Foundation, and any use by you of this program is subject to the terms
# of such GNU licence.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, you can access it online at
# http://www.gnu.org/licenses/gpl-2.0.html.
#
# SPDX-License-Identifier: GPL-2.0
#
#
obj-y += gsl/
obj-y += novatek/
obj-y += focaltech/
lct_tp-objs := lct_tp_info.o
lct_tp-objs += lct_tp_work.o
lct_tp-objs += lct_tp_selftest.o
lct_tp-objs += lct_tp_palm.o
lct_tp-objs += lct_tp_grip_area.o
lct_tp-objs += lct_tp_gesture.o
obj-$(CONFIG_TOUCHSCREEN_LCT) += lct_tp.o
