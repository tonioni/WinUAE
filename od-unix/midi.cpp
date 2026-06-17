#include "sysconfig.h"
#include "sysdeps.h"

#ifdef WITH_MIDI

#include <deque>
#include <mutex>
#include <vector>

#if defined(WINUAE_UNIX_WITH_COREMIDI)
#include <CoreFoundation/CoreFoundation.h>
#include <CoreMIDI/CoreMIDI.h>
#elif defined(WINUAE_UNIX_WITH_ALSA_MIDI)
#include <alsa/asoundlib.h>
#endif

#include "options.h"
#include "midi.h"
#ifdef WITH_MIDIEMU
#include "midiemu.h"
#endif

extern int serdev;

BOOL midi_ready = FALSE;

struct unix_midi_output_device {
    int devid;
    TCHAR name[256];
    TCHAR label[256];
    bool emulated;
#if defined(WINUAE_UNIX_WITH_COREMIDI)
    MIDIEndpointRef endpoint;
#elif defined(WINUAE_UNIX_WITH_ALSA_MIDI)
    int client;
    int port;
#endif
};

static std::vector<unix_midi_output_device> midi_outputs;
static std::vector<unix_midi_output_device> midi_inputs;
static bool midi_outputs_enumerated;
static bool midi_inputs_enumerated;
static MidiOutStatus out_status;
static std::vector<uae_u8> sysex_buffer;
static std::deque<uae_u8> input_queue;
static std::mutex input_queue_mutex;

#if defined(WINUAE_UNIX_WITH_COREMIDI)
static MIDIClientRef midi_client;
static MIDIPortRef midi_out_port;
static MIDIEndpointRef midi_out_endpoint;
static MIDIPortRef midi_in_port;
static MIDIEndpointRef midi_in_endpoint;
#elif defined(WINUAE_UNIX_WITH_ALSA_MIDI)
static snd_seq_t *alsa_seq;
static int alsa_out_port = -1;
static int alsa_in_port = -1;
static snd_midi_event_t *alsa_encoder;
static snd_midi_event_t *alsa_decoder;
#endif
static bool midi_in_ready;

static const uae_u8 plen[128] = {
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
    2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,
    1,1,2,1,0,0,0,0,0,0,0,0,0,0,0,0
};

static void midi_reset_parser(void)
{
    memset(&out_status, 0, sizeof out_status);
    sysex_buffer.clear();
}

static void midi_clear_input_queue(void)
{
    std::lock_guard<std::mutex> lock(input_queue_mutex);
    input_queue.clear();
}

static void enqueue_midi_input_bytes(const uae_u8 *data, int len)
{
    if (!data || len <= 0) {
        return;
    }
    std::lock_guard<std::mutex> lock(input_queue_mutex);
    for (int i = 0; i < len; i++) {
        if (input_queue.size() >= BUFFLEN) {
            input_queue.pop_front();
        }
        input_queue.push_back(data[i]);
        if (currprefs.win32_midirouter && midi_ready) {
            BYTE out = data[i];
            Midi_Parse(midi_output, &out);
        }
    }
}

#if defined(WINUAE_UNIX_WITH_COREMIDI)
static void coremidi_input_proc(const MIDIPacketList *packet_list, void*, void*)
{
    const MIDIPacket *packet = &packet_list->packet[0];
    for (UInt32 i = 0; i < packet_list->numPackets; i++) {
        enqueue_midi_input_bytes(packet->data, packet->length);
        packet = MIDIPacketNext(packet);
    }
}
#endif

#if defined(WINUAE_UNIX_WITH_COREMIDI)
static bool coremidi_object_string(MIDIObjectRef object, CFStringRef property, TCHAR *out, size_t out_size)
{
    if (!out || out_size == 0) {
        return false;
    }
    out[0] = 0;
    CFStringRef str = NULL;
    if (MIDIObjectGetStringProperty(object, property, &str) != noErr || !str) {
        return false;
    }
    bool ok = CFStringGetCString(str, out, out_size, kCFStringEncodingUTF8);
    CFRelease(str);
    return ok && out[0];
}
#endif

static void enumerate_midi_outputs(void)
{
    midi_outputs.clear();
    midi_outputs_enumerated = true;
#if defined(WINUAE_UNIX_WITH_COREMIDI)
    const ItemCount count = MIDIGetNumberOfDestinations();
    for (ItemCount i = 0; i < count; i++) {
        MIDIEndpointRef endpoint = MIDIGetDestination(i);
        if (!endpoint) {
            continue;
        }
        unix_midi_output_device dev;
        memset(&dev, 0, sizeof dev);
        dev.devid = (int)i;
        dev.endpoint = endpoint;
        if (!coremidi_object_string(endpoint, kMIDIPropertyDisplayName, dev.name, sizeof dev.name / sizeof(TCHAR))
            && !coremidi_object_string(endpoint, kMIDIPropertyName, dev.name, sizeof dev.name / sizeof(TCHAR))) {
            _sntprintf(dev.name, sizeof dev.name / sizeof(TCHAR), _T("CoreMIDI destination %d"), (int)i + 1);
        }
        midi_outputs.push_back(dev);
    }
#elif defined(WINUAE_UNIX_WITH_ALSA_MIDI)
    snd_seq_t *seq = NULL;
    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_OUTPUT, 0) < 0) {
        return;
    }
    snd_seq_client_info_t *client_info;
    snd_seq_port_info_t *port_info;
    snd_seq_client_info_alloca(&client_info);
    snd_seq_port_info_alloca(&port_info);
    snd_seq_client_info_set_client(client_info, -1);
    int devid = 0;
    while (snd_seq_query_next_client(seq, client_info) >= 0) {
        int client = snd_seq_client_info_get_client(client_info);
        snd_seq_port_info_set_client(port_info, client);
        snd_seq_port_info_set_port(port_info, -1);
        while (snd_seq_query_next_port(seq, port_info) >= 0) {
            unsigned int caps = snd_seq_port_info_get_capability(port_info);
            if ((caps & SND_SEQ_PORT_CAP_WRITE) == 0 || (caps & SND_SEQ_PORT_CAP_SUBS_WRITE) == 0) {
                continue;
            }
            unix_midi_output_device dev;
            memset(&dev, 0, sizeof dev);
            dev.devid = devid++;
            dev.client = client;
            dev.port = snd_seq_port_info_get_port(port_info);
            const char *client_name = snd_seq_client_info_get_name(client_info);
            const char *port_name = snd_seq_port_info_get_name(port_info);
            _sntprintf(dev.name, sizeof dev.name / sizeof(TCHAR), _T("%d:%d %s%s%s"),
                dev.client, dev.port,
                client_name ? client_name : "ALSA",
                port_name && port_name[0] ? " " : "",
                port_name ? port_name : "");
            midi_outputs.push_back(dev);
        }
    }
    snd_seq_close(seq);
#endif
#ifdef WITH_MIDIEMU
    unix_midi_output_device dev;
    memset(&dev, 0, sizeof dev);
    dev.devid = (int)midi_outputs.size();
    dev.emulated = true;
    _tcscpy(dev.name, _T("Munt MT-32"));
    _tcscpy(dev.label, midi_emu_available(_T("MT-32")) ? _T("Munt MT-32") : _T("Munt MT-32 (Missing ROMs)"));
    midi_outputs.push_back(dev);

    memset(&dev, 0, sizeof dev);
    dev.devid = (int)midi_outputs.size();
    dev.emulated = true;
    _tcscpy(dev.name, _T("Munt CM-32L"));
    _tcscpy(dev.label, midi_emu_available(_T("CM-32L")) ? _T("Munt CM-32L") : _T("Munt CM-32L (Missing ROMs)"));
    midi_outputs.push_back(dev);
#endif
}

static void enumerate_midi_inputs(void)
{
    midi_inputs.clear();
    midi_inputs_enumerated = true;
#if defined(WINUAE_UNIX_WITH_COREMIDI)
    const ItemCount count = MIDIGetNumberOfSources();
    for (ItemCount i = 0; i < count; i++) {
        MIDIEndpointRef endpoint = MIDIGetSource(i);
        if (!endpoint) {
            continue;
        }
        unix_midi_output_device dev;
        memset(&dev, 0, sizeof dev);
        dev.devid = (int)i;
        dev.endpoint = endpoint;
        if (!coremidi_object_string(endpoint, kMIDIPropertyDisplayName, dev.name, sizeof dev.name / sizeof(TCHAR))
            && !coremidi_object_string(endpoint, kMIDIPropertyName, dev.name, sizeof dev.name / sizeof(TCHAR))) {
            _sntprintf(dev.name, sizeof dev.name / sizeof(TCHAR), _T("CoreMIDI source %d"), (int)i + 1);
        }
        midi_inputs.push_back(dev);
    }
#elif defined(WINUAE_UNIX_WITH_ALSA_MIDI)
    snd_seq_t *seq = NULL;
    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_INPUT, 0) < 0) {
        return;
    }
    snd_seq_client_info_t *client_info;
    snd_seq_port_info_t *port_info;
    snd_seq_client_info_alloca(&client_info);
    snd_seq_port_info_alloca(&port_info);
    snd_seq_client_info_set_client(client_info, -1);
    int devid = 0;
    while (snd_seq_query_next_client(seq, client_info) >= 0) {
        int client = snd_seq_client_info_get_client(client_info);
        snd_seq_port_info_set_client(port_info, client);
        snd_seq_port_info_set_port(port_info, -1);
        while (snd_seq_query_next_port(seq, port_info) >= 0) {
            unsigned int caps = snd_seq_port_info_get_capability(port_info);
            if ((caps & SND_SEQ_PORT_CAP_READ) == 0 || (caps & SND_SEQ_PORT_CAP_SUBS_READ) == 0) {
                continue;
            }
            unix_midi_output_device dev;
            memset(&dev, 0, sizeof dev);
            dev.devid = devid++;
            dev.client = client;
            dev.port = snd_seq_port_info_get_port(port_info);
            const char *client_name = snd_seq_client_info_get_name(client_info);
            const char *port_name = snd_seq_port_info_get_name(port_info);
            _sntprintf(dev.name, sizeof dev.name / sizeof(TCHAR), _T("%d:%d %s%s%s"),
                dev.client, dev.port,
                client_name ? client_name : "ALSA",
                port_name && port_name[0] ? " " : "",
                port_name ? port_name : "");
            midi_inputs.push_back(dev);
        }
    }
    snd_seq_close(seq);
#endif
}

static void ensure_midi_outputs(void)
{
    if (!midi_outputs_enumerated) {
        enumerate_midi_outputs();
    }
}

static void ensure_midi_inputs(void)
{
    if (!midi_inputs_enumerated) {
        enumerate_midi_inputs();
    }
}

static unix_midi_output_device *find_midi_device(std::vector<unix_midi_output_device> &devices, int devid, bool default_to_first)
{
    if (devices.empty()) {
        return NULL;
    }
    if (default_to_first && devid == -1) {
        for (size_t i = 0; i < devices.size(); i++) {
            if (!devices[i].emulated) {
                return &devices[i];
            }
        }
        return NULL;
    }
    for (size_t i = 0; i < devices.size(); i++) {
        if (devices[i].devid == devid) {
            return &devices[i];
        }
    }
    return NULL;
}

static unix_midi_output_device *find_midi_output(int devid)
{
    ensure_midi_outputs();
    return find_midi_device(midi_outputs, devid, true);
}

static unix_midi_output_device *find_midi_input(int devid)
{
    ensure_midi_inputs();
    return find_midi_device(midi_inputs, devid, false);
}

static bool has_native_midi_output(void)
{
    ensure_midi_outputs();
    for (size_t i = 0; i < midi_outputs.size(); i++) {
        if (!midi_outputs[i].emulated) {
            return true;
        }
    }
    return false;
}

int unix_midi_output_device_count(void)
{
    ensure_midi_outputs();
    return (int)midi_outputs.size() + (has_native_midi_output() ? 1 : 0);
}

int unix_midi_output_device_id(int index)
{
    ensure_midi_outputs();
    const bool has_default = has_native_midi_output();
    if (index == 0 && has_default) {
        return -1;
    }
    if (has_default) {
        index--;
    }
    if (index < 0 || index >= (int)midi_outputs.size()) {
        return -2;
    }
    return midi_outputs[index].devid;
}

const TCHAR *unix_midi_output_device_display_name(int index)
{
    static TCHAR name[320];
    ensure_midi_outputs();
    const bool has_default = has_native_midi_output();
    if (index == 0 && has_default) {
        return _T("Default MIDI-Out Device");
    }
    if (has_default) {
        index--;
    }
    if (index < 0 || index >= (int)midi_outputs.size()) {
        return _T("");
    }
    _sntprintf(name, sizeof name / sizeof(TCHAR), _T("%s"),
        midi_outputs[index].label[0] ? midi_outputs[index].label : midi_outputs[index].name);
    return name;
}

const TCHAR *unix_midi_output_device_config_name_for_id(int devid)
{
    if (devid < -1) {
        return _T("none");
    }
    if (devid == -1) {
        return _T("default");
    }
    unix_midi_output_device *dev = find_midi_output(devid);
    return dev ? dev->name : _T("default");
}

int unix_midi_output_device_id_from_config_name(const TCHAR *name)
{
    if (!name || !name[0] || !_tcsicmp(name, _T("none"))) {
        return -2;
    }
    if (!_tcsicmp(name, _T("default"))) {
        return -1;
    }
    ensure_midi_outputs();
    for (size_t i = 0; i < midi_outputs.size(); i++) {
        if (!_tcsicmp(name, midi_outputs[i].name)) {
            return midi_outputs[i].devid;
        }
    }
    return -2;
}

int unix_midi_input_device_count(void)
{
    ensure_midi_inputs();
    return (int)midi_inputs.size();
}

int unix_midi_input_device_id(int index)
{
    ensure_midi_inputs();
    if (index < 0 || index >= (int)midi_inputs.size()) {
        return -1;
    }
    return midi_inputs[index].devid;
}

const TCHAR *unix_midi_input_device_display_name(int index)
{
    static TCHAR name[320];
    ensure_midi_inputs();
    if (index < 0 || index >= (int)midi_inputs.size()) {
        return _T("");
    }
    _sntprintf(name, sizeof name / sizeof(TCHAR), _T("%s"), midi_inputs[index].name);
    return name;
}

const TCHAR *unix_midi_input_device_config_name_for_id(int devid)
{
    if (devid < 0) {
        return _T("none");
    }
    unix_midi_output_device *dev = find_midi_input(devid);
    return dev ? dev->name : _T("none");
}

int unix_midi_input_device_id_from_config_name(const TCHAR *name)
{
    if (!name || !name[0] || !_tcsicmp(name, _T("none"))) {
        return -1;
    }
    ensure_midi_inputs();
    for (size_t i = 0; i < midi_inputs.size(); i++) {
        if (!_tcsicmp(name, midi_inputs[i].name)) {
            return midi_inputs[i].devid;
        }
    }
    return -1;
}

static bool send_midi_bytes(const uae_u8 *data, int len)
{
    if (!data || len <= 0 || !midi_ready) {
        return false;
    }
#if defined(WINUAE_UNIX_WITH_COREMIDI)
    std::vector<Byte> packet_storage(sizeof(MIDIPacketList) + len + 128);
    MIDIPacketList *packet_list = (MIDIPacketList*)packet_storage.data();
    MIDIPacket *packet = MIDIPacketListInit(packet_list);
    packet = MIDIPacketListAdd(packet_list, packet_storage.size(), packet, 0, len, data);
    if (!packet) {
        return false;
    }
    return MIDISend(midi_out_port, midi_out_endpoint, packet_list) == noErr;
#elif defined(WINUAE_UNIX_WITH_ALSA_MIDI)
    bool sent = false;
    for (int i = 0; i < len; i++) {
        snd_seq_event_t ev;
        snd_seq_ev_clear(&ev);
        int ret = snd_midi_event_encode_byte(alsa_encoder, data[i], &ev);
        if (ret > 0) {
            snd_seq_ev_set_source(&ev, alsa_out_port);
            snd_seq_ev_set_subs(&ev);
            snd_seq_ev_set_direct(&ev);
            if (snd_seq_event_output_direct(alsa_seq, &ev) >= 0) {
                sent = true;
            }
        }
    }
    return sent;
#else
    return false;
#endif
}

static uae_u8 midi_scale_volume(uae_u8 value)
{
    int volume = currprefs.sound_volume_midi;
    if (volume <= 0) {
        return value;
    }
    if (volume >= 100) {
        return 0;
    }
    return (uae_u8)((int)value * (100 - volume) / 100);
}

static void midi_apply_output_volume(uae_u8 *msg, int len)
{
    if (!msg || len < 2 || currprefs.sound_volume_midi <= 0) {
        return;
    }
    switch (msg[0] & 0xf0)
    {
    case 0x80:
    case 0x90:
    case 0xa0:
        if (len >= 3) {
            msg[2] = midi_scale_volume(msg[2]);
        }
        break;
    case 0xd0:
        msg[1] = midi_scale_volume(msg[1]);
        break;
    }
}

#if defined(WINUAE_UNIX_WITH_ALSA_MIDI)
static void poll_alsa_midi_input(void)
{
    if (!midi_in_ready || !alsa_seq || !alsa_decoder) {
        return;
    }
    while (snd_seq_event_input_pending(alsa_seq, 1) > 0) {
        snd_seq_event_t *ev = NULL;
        if (snd_seq_event_input(alsa_seq, &ev) < 0 || !ev) {
            break;
        }
        uae_u8 data[256];
        long len = snd_midi_event_decode(alsa_decoder, data, sizeof data, ev);
        if (len > 0) {
            enqueue_midi_input_bytes(data, (int)len);
        }
    }
}
#else
static void poll_alsa_midi_input(void)
{
}
#endif

int Midi_Parse(midi_direction_e direction, BYTE *dataptr)
{
    if (direction != midi_output || !dataptr) {
        return 0;
    }
    const uae_u8 data = (uae_u8)*dataptr;
    if (data >= 0x80) {
        if (data < MIDI_CLOCK && out_status.sysex) {
            sysex_buffer.push_back(MIDI_EOX);
            send_midi_bytes(sysex_buffer.data(), (int)sysex_buffer.size());
            sysex_buffer.clear();
            out_status.sysex = 0;
            out_status.unknown = 1;
            if (data == MIDI_EOX) {
                return 0;
            }
        }
        out_status.status = data;
        out_status.length = plen[data & 0x7f];
        out_status.posn = 0;
        out_status.unknown = 0;
        if (data == MIDI_SYSX) {
            out_status.sysex = 1;
            sysex_buffer.clear();
            sysex_buffer.push_back(data);
            return 0;
        }
        if (out_status.length == 0) {
            send_midi_bytes(&data, 1);
        }
        return 0;
    }
    if (out_status.sysex) {
        if (sysex_buffer.size() < BUFFLEN) {
            sysex_buffer.push_back(data);
        }
        return 0;
    }
    if (out_status.unknown) {
        return 0;
    }
    if (++out_status.posn == 1) {
        out_status.byte1 = data;
    } else {
        out_status.byte2 = data;
    }
    if (out_status.posn >= out_status.length) {
        uae_u8 msg[3] = {
            (uae_u8)out_status.status,
            (uae_u8)out_status.byte1,
            (uae_u8)out_status.byte2
        };
        const int len = 1 + out_status.length;
        out_status.posn = 0;
        midi_apply_output_volume(msg, len);
        send_midi_bytes(msg, len);
    }
    return 0;
}

int Midi_Open(void)
{
    if (midi_ready) {
        return 1;
    }
    if (currprefs.win32_midioutdev < -1) {
        return 0;
    }
    unix_midi_output_device *outdev = find_midi_output(currprefs.win32_midioutdev);
    unix_midi_output_device *indev = currprefs.win32_midiindev >= 0 ? find_midi_input(currprefs.win32_midiindev) : NULL;
    if (!outdev) {
        write_log(_T("MIDI OUT: no output device for id %d\n"), currprefs.win32_midioutdev);
        return 0;
    }
    if (outdev->emulated) {
        return 0;
    }
#if defined(WINUAE_UNIX_WITH_COREMIDI)
    if (MIDIClientCreate(CFSTR("WinUAE Unix MIDI"), NULL, NULL, &midi_client) != noErr) {
        write_log(_T("MIDI OUT: MIDIClientCreate failed\n"));
        return 0;
    }
    if (MIDIOutputPortCreate(midi_client, CFSTR("WinUAE Unix MIDI Out"), &midi_out_port) != noErr) {
        MIDIClientDispose(midi_client);
        midi_client = 0;
        write_log(_T("MIDI OUT: MIDIOutputPortCreate failed\n"));
        return 0;
    }
    midi_out_endpoint = outdev->endpoint;
    if (indev) {
        if (MIDIInputPortCreate(midi_client, CFSTR("WinUAE Unix MIDI In"), coremidi_input_proc, NULL, &midi_in_port) == noErr
            && MIDIPortConnectSource(midi_in_port, indev->endpoint, NULL) == noErr) {
            midi_in_endpoint = indev->endpoint;
            midi_in_ready = true;
        } else {
            if (midi_in_port) {
                MIDIPortDispose(midi_in_port);
                midi_in_port = 0;
            }
            write_log(_T("MIDI IN: failed to open %s\n"), indev->name);
        }
    }
#elif defined(WINUAE_UNIX_WITH_ALSA_MIDI)
    if (snd_seq_open(&alsa_seq, "default", indev ? SND_SEQ_OPEN_DUPLEX : SND_SEQ_OPEN_OUTPUT, 0) < 0) {
        write_log(_T("MIDI OUT: failed to open ALSA sequencer\n"));
        return 0;
    }
    snd_seq_set_client_name(alsa_seq, "WinUAE Unix MIDI");
    alsa_out_port = snd_seq_create_simple_port(alsa_seq, "WinUAE MIDI Out",
        SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    if (alsa_out_port < 0 || snd_seq_connect_to(alsa_seq, alsa_out_port, outdev->client, outdev->port) < 0) {
        snd_seq_close(alsa_seq);
        alsa_seq = NULL;
        alsa_out_port = -1;
        write_log(_T("MIDI OUT: failed to connect ALSA port %s\n"), outdev->name);
        return 0;
    }
    if (snd_midi_event_new(BUFFLEN, &alsa_encoder) < 0) {
        snd_seq_close(alsa_seq);
        alsa_seq = NULL;
        alsa_out_port = -1;
        write_log(_T("MIDI OUT: failed to create ALSA MIDI encoder\n"));
        return 0;
    }
    snd_midi_event_init(alsa_encoder);
    if (indev) {
        alsa_in_port = snd_seq_create_simple_port(alsa_seq, "WinUAE MIDI In",
            SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
            SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
        if (alsa_in_port >= 0
            && snd_seq_connect_from(alsa_seq, alsa_in_port, indev->client, indev->port) >= 0
            && snd_midi_event_new(BUFFLEN, &alsa_decoder) >= 0) {
            snd_midi_event_init(alsa_decoder);
            snd_seq_nonblock(alsa_seq, 1);
            midi_in_ready = true;
        } else {
            if (alsa_decoder) {
                snd_midi_event_free(alsa_decoder);
                alsa_decoder = NULL;
            }
            write_log(_T("MIDI IN: failed to connect ALSA port %s\n"), indev->name);
        }
    }
#endif
    midi_reset_parser();
    midi_clear_input_queue();
    midi_ready = TRUE;
    serdev = 1;
    write_log(_T("MIDI OUT: using %s\n"), outdev->name);
    if (midi_in_ready && indev) {
        write_log(_T("MIDI IN: using %s\n"), indev->name);
    }
    return 1;
}

void Midi_Close(void)
{
    if (!midi_ready) {
        return;
    }
#if defined(WINUAE_UNIX_WITH_COREMIDI)
    if (midi_in_port) {
        if (midi_in_endpoint) {
            MIDIPortDisconnectSource(midi_in_port, midi_in_endpoint);
        }
        MIDIPortDispose(midi_in_port);
        midi_in_port = 0;
    }
    midi_in_endpoint = 0;
    if (midi_out_port) {
        MIDIPortDispose(midi_out_port);
        midi_out_port = 0;
    }
    if (midi_client) {
        MIDIClientDispose(midi_client);
        midi_client = 0;
    }
    midi_out_endpoint = 0;
#elif defined(WINUAE_UNIX_WITH_ALSA_MIDI)
    if (alsa_decoder) {
        snd_midi_event_free(alsa_decoder);
        alsa_decoder = NULL;
    }
    if (alsa_encoder) {
        snd_midi_event_free(alsa_encoder);
        alsa_encoder = NULL;
    }
    if (alsa_seq) {
        snd_seq_close(alsa_seq);
        alsa_seq = NULL;
    }
    alsa_out_port = -1;
    alsa_in_port = -1;
#endif
    midi_in_ready = false;
    midi_ready = FALSE;
    midi_reset_parser();
    midi_clear_input_queue();
    write_log(_T("MIDI: closed.\n"));
}

void Midi_Reopen(void)
{
    if (midi_ready) {
        Midi_Close();
        Midi_Open();
    }
}

int ismidibyte(void)
{
    poll_alsa_midi_input();
    std::lock_guard<std::mutex> lock(input_queue_mutex);
    return input_queue.empty() ? 0 : 1;
}

LONG getmidibyte(void)
{
    poll_alsa_midi_input();
    std::lock_guard<std::mutex> lock(input_queue_mutex);
    if (input_queue.empty()) {
        return -1;
    }
    LONG value = input_queue.front();
    input_queue.pop_front();
    return value;
}

#endif
