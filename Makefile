# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
##########################################################################

RM          := rm -rf
CXXFLAGS    = -std=c++1y  -g -fPIC -D_REENTRANT -Wall -DALSA_AUDIO_MASTER_CONTROL_ENABLE
LIBNAME     := ds-hal
LIBNAMEFULL := lib$(LIBNAME).so
OBJS        := $(patsubst %.c,%.o,$(wildcard *.c))
DSHAL_API_MAJOR_VERSION := '0'
DSHAL_API_MINOR_VERSION := '0'
VERSION     := $(DSHAL_API_MAJOR_VERSION).$(DSHAL_API_MINOR_VERSION)
LIBSOM = $(LIBNAMEFULL).$(DSHAL_API_MAJOR_VERSION)
LIBSOV = $(LIBNAMEFULL).$(VERSION)

# Source and object files for change_resolution
CHANGE_RESOLUTION_SRC := change_resolutions.c
CHANGE_RESOLUTION_OBJ := $(CHANGE_RESOLUTION_SRC:.c=.o)
CHANGE_RESOLUTION_BIN := change_resolutions

$(LIBNAMEFULL): $(LIBSOV)
    ln -sf $(LIBSOV) $(LIBNAMEFULL)
    ln -sf $(LIBSOV) $(LIBSOM)

$(LIBSOV): $(OBJS)
    @echo "Building $(LIBSOV) ...."
    $(CXX) $(OBJS) -shared -Wl,-soname,$(LIBSOM) -o $(LIBSOV) -lvchostif -lvchiq_arm -lvcos -lasound

%.o: %.c
    @echo "Building $@ ...."
    $(CXX) -c $<  $(CXXFLAGS)  -DALSA_AUDIO_MASTER_CONTROL_ENABLE -I=/usr/include/interface/vmcs_host/linux $(CFLAGS) -o $@

$(CHANGE_RESOLUTION_BIN): $(CHANGE_RESOLUTION_OBJ)
    @echo "Building $@ ...."
    $(CC) $(CHANGE_RESOLUTION_OBJ) -o $@ -ldrm

install: $(LIBSOV) $(CHANGE_RESOLUTION_BIN)
    @echo "Installing files in $(DESTDIR) ..."
    install -d $(DESTDIR)
    install -m 0644 $(LIBSOV) $(DESTDIR)
    install -m 0755 $(CHANGE_RESOLUTION_BIN) $(DESTDIR)

.PHONY: clean
clean:
    $(RM) *.so* *.o $(CHANGE_RESOLUTION_BIN)