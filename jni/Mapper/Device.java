
package Mapper;

public class Device
{
    public Device(String name, int port) {
        _device = mdev_new(name, port);
    }

    public void free() {
        if (_device!=0)
            mdev_free(_device);
        _device = 0;
    }

    public int poll(int timeout) {
        return mdev_poll(_device, timeout);
    }

    public class Signal {
        private Signal(long s, Device d) { _signal = s; _device = d; }

        public String name()
        {
            checkDevice();
            return msig_name(_signal);
        }
        public String full_name()
        {
            checkDevice();
            return msig_full_name(_signal);
        }
        public boolean is_output()
        {
            checkDevice();
            return msig_is_output(_signal);
        }
        private native String msig_full_name(long sig);
        private native String msig_name(long sig);
        private native boolean msig_is_output(long sig);

        public int index()
        {
            checkDevice();
            if (_index==null) {
                _index = new Integer(-1);
                long msig = 0;
                if (is_output())
                    msig = mdev_get_output_by_name(_device._device,
                                                   msig_name(_signal),
                                                   _index);
                else
                    msig = mdev_get_input_by_name(_device._device,
                                                  msig_name(_signal),
                                                  _index);
                if (msig==0)
                    _index = null;
            }
            return _index;
        }

        private void checkDevice() {
            if (_device._device == 0)
                throw new NullPointerException(
                    "Signal object associated with invalid Device");
        }

        private long _signal;
        private Device _device;
        private Integer _index;
    };

    public Signal add_input(String name, int length, char type,
                            String unit, Double minimum,
                            Double maximum, InputListener handler)
    {
        long msig = mdev_add_input(_device, name, length, type, unit,
                                   minimum, maximum, handler);
        return msig==0 ? null : new Signal(msig, this);
    }

    public Signal add_output(String name, int length, char type,
                             String unit, Double minimum,
                             Double maximum)
    {
        long msig = mdev_add_output(_device, name, length, type, unit,
                                    minimum, maximum);
        return msig==0 ? null : new Signal(msig, this);
    }

    public void remove_input(Signal sig)
    {
        mdev_remove_input(_device, sig._signal);
    }

    public void remove_output(Signal sig)
    {
        mdev_remove_output(_device, sig._signal);
    }

    public int num_inputs()
    {
        return mdev_num_inputs(_device);
    }

    public int num_outputs()
    {
        return mdev_num_outputs(_device);
    }

    public Signal get_input_by_name(String name)
    {
        long msig = mdev_get_input_by_name(_device, name, null);
        return msig==0 ? null : new Signal(msig, this);
    }

    public Signal get_output_by_name(String name)
    {
        long msig = mdev_get_output_by_name(_device, name, null);
        return msig==0 ? null : new Signal(msig, this);
    }

    public Signal get_input_by_index(int index)
    {
        long msig = mdev_get_input_by_index(_device, index);
        return msig==0 ? null : new Signal(msig, this);
    }

    public Signal get_output_by_index(int index)
    {
        long msig = mdev_get_output_by_index(_device, index);
        return msig==0 ? null : new Signal(msig, this);
    }

    /* TODO: Properties, need a class to represent lo_arg.
    public void set_property(String property, char type, double value)
    {
        mdev_set_property(_device, property, type, double value);
    }
    public void remove_property(String property)
    {
        mdev_remove_property(_device, String property);
    }
    */

    public boolean ready()
    {
        return mdev_ready(_device);
    }

    public String name()
    {
        return mdev_name(_device);
    }

    public int port()
    {
        return mdev_port(_device);
    }

    public String ip4()
    {
        return mdev_ip4(_device);
    }

    public String iface()
    {
        return mdev_interface(_device);
    }

    public int ordinal()
    {
        return mdev_ordinal(_device);
    }

    // Note: this is _not_ guaranteed to run, the user should still
    // call free() explicitly when the device is no longer needed.
    protected void finalize() throws Throwable {
        try {
            free();
        } finally {
            super.finalize();
        }
    }

    private native long mdev_new(String name, int port);
    private native void mdev_free(long _d);
    private native int mdev_poll(long _d, int timeout);
    private native long mdev_add_input(long _d, String name, int length,
                                       char type, String unit,
                                       Double minimum, Double maximum,
                                       InputListener handler);
    private native long mdev_add_output(long _d, String name, int length,
                                        char type, String unit,
                                        Double minimum, Double maximum);
    private native void mdev_remove_input(long _d, long _sig);
    private native void mdev_remove_output(long _d, long _sig);
    private native int mdev_num_inputs(long _d);
    private native int mdev_num_outputs(long _d);
    private native long mdev_get_input_by_name(long _d, String name,
                                               Integer index);
    private native long mdev_get_output_by_name(long _d, String name,
                                                Integer index);
    private native long mdev_get_input_by_index(long _d, int index);
    private native long mdev_get_output_by_index(long _d, int index);
    /*
    private native void mdev_set_property(long _d, String property,
                                          lo_type type, lo_arg *value);
    private native void mdev_remove_property(long _d, String property);
    */
    private native boolean mdev_ready(long _d);
    private native String mdev_name(long _d);
    private native int mdev_port(long _d);
    private native String mdev_ip4(long _d);
    private native String mdev_interface(long _d);
    private native int mdev_ordinal(long _d);

    private long _device;

    static { 
        System.loadLibrary("mapperjni-0");
    } 
}
