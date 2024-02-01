CPPFLAGS += -g -DFSUAE -I . -I include -I od-cross -MMD

OUT=out/$(target)

objs = $(srcs:%.cpp=$(OUT)/%.o)
deps = $(objs:.o=.d)

# bash Create Dir rule
MD = mkdir -p $(1) > /dev/null

# Create output dirs
dirs:=$(sort $(OUT) $(patsubst %/,%,$(dir $(objs))))
$(foreach dir,$(dirs),$(shell $(call MD,$(dir))))

.PHONY: all clean

all: $(target)

clean:
	rm -f $(objs) $(deps) $(target) -r $(OUT)

$(objs): $(OUT)/%.o : %.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(TARGET_ARCH) -c -o $@ $<

$(target) : $(objs)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(LDFLAGS) $(TARGET_ARCH) $^ $(LOADLIBES) $(LDLIBS) -o $@

-include $(objs:.o=.d)
