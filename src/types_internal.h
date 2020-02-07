
#ifndef __MPR_TYPES_H__
#define __MPR_TYPES_H__

#include <lo/lo_lowlevel.h>

#include "config.h"

#ifdef HAVE_ARPA_INET_H
 #include <arpa/inet.h>
#else
 #ifdef HAVE_WINSOCK2_H
  #include <winsock2.h>
 #endif
#endif

#include <mpr/mpr_constants.h>

#ifdef HAVE_INTTYPES_H
#include <inttypes.h>
#define PR_MPR_ID PRIu64
#else
#define PR_MPR_ID "llu"
#endif

/**** Defined in mpr.h ****/

/* Types defined here replace opaque prototypes in mpr.h, thus we cannot
 * include it here.  Instead we include some prototypes here. Typedefs cannot
 * be repeated, therefore they are refered to by struct name. */

typedef struct _mpr_expr *mpr_expr;

/* Forward declarations for this file. */

struct _mpr_obj;
typedef struct _mpr_obj **mpr_list;
struct _mpr_dev;
typedef struct _mpr_dev mpr_dev_t;
typedef struct _mpr_dev *mpr_dev;
struct _mpr_sig;
typedef struct _mpr_sig mpr_sig_t;
typedef struct _mpr_sig *mpr_sig;
struct _mpr_link;
typedef struct _mpr_link mpr_link_t;
typedef struct _mpr_link *mpr_link;
struct _mpr_map;
typedef struct _mpr_map mpr_map_t;
typedef struct _mpr_map *mpr_map;
struct _mpr_allocated_t;
struct _mpr_map;
struct _mpr_id_map;
typedef int mpr_sig_group;

/**** String tables ****/

// bit flags for tracking permissions for modifying properties
#define NON_MODIFIABLE      0x00
#define LOCAL_MODIFY        0x01
#define REMOTE_MODIFY       0x02
#define MODIFIABLE          0x03
#define LOCAL_ACCESS_ONLY   0x04
#define MUTABLE_TYPE        0x08
#define MUTABLE_LENGTH      0x10
#define INDIRECT            0x20
#define PROP_OWNED          0x40
#define PROP_DIRTY          0x80

/*! Used to hold look-up table records. */
typedef struct {
    const char *key;
    void **val;
    int len;
    mpr_prop prop;
    mpr_type type;
    char flags;
} mpr_tbl_record_t, *mpr_tbl_record;

/*! Used to hold look-up tables. */
typedef struct _mpr_tbl {
    mpr_tbl_record rec;
    int count;
    int alloced;
    char dirty;
} mpr_tbl_t, *mpr_tbl;

typedef struct _mpr_dict {
    struct _mpr_tbl *synced;
    struct _mpr_tbl *staged;
    int mask;
} mpr_dict_t, *mpr_dict;

/**** Graph ****/

/*! A list of function and context pointers. */
typedef struct _fptr_list {
    void *f;
    void *ctx;
    struct _fptr_list *next;
    int types;
} *fptr_list;

typedef struct _mpr_subscription {
    struct _mpr_subscription *next;
    mpr_dev dev;
    int flags;
    uint32_t lease_expiration_sec;
} *mpr_subscription;

/*! A structure that keeps information about network communications. */
typedef struct _mpr_net {
    struct _mpr_graph *graph;
    struct {
        union {
            lo_server all[4];
            struct {
                lo_server_thread admin[2];
                lo_server dev[2];
            };
            struct {
                lo_server_thread bus;   /*!< LibLo server for the multicast. */
                lo_server_thread mesh;  /*!< LibLo server for mesh comms. */

                /*! Servers used to handle incoming signal messages. */
                lo_server udp;
                lo_server tcp;
            };
        };
    } server;

    struct {
        lo_address bus;             /*!< LibLo address for the multicast bus. */
        lo_address dst;
        struct _mpr_dev *dev;
    } addr;

    struct {
        char *name;                 /*!< The name of the network interface
                                     *   for receiving messages. */
        struct in_addr addr;        /*!< The IP address of interface. */
    } iface;

    struct _mpr_dev **devs;         /*!< Local devices managed by this network
                                     *   structure. */
    lo_bundle bundle;               /*!< Bundle pointer for sending
                                     *   messages on the multicast bus. */

    struct {
        char *group;
        int port;
    } multicast;

    struct _mpr_rtr *rtr;

    int random_id;                  /*!< Random id for allocation speedup. */
    int msgs_recvd;                 /*!< 1 if messages have been received on the
                                     *   multicast bus/mesh. */
    int msg_type;
    int num_devs;
    uint32_t next_bus_ping;
    uint32_t next_sub_ping;
    uint8_t graph_methods_added;
} mpr_net_t, *mpr_net;

typedef struct _mpr_graph {
    mpr_net_t net;
    mpr_list devs;                   //<! List of devices.
    mpr_list sigs;                   //<! List of signals.
    mpr_list maps;                   //<! List of maps.
    mpr_list links;                  //<! List of links.
    fptr_list callbacks;             //<! List of object record callbacks.

    /*! Linked-list of autorenewing device subscriptions. */
    mpr_subscription subscriptions;

    /*! Flags indicating whether information on signals and mappings should
     *  be automatically subscribed to when a new device is seen.*/
    int autosub;

    int own;
    int staged_maps;

    uint32_t resource_counter;
} mpr_graph_t, *mpr_graph;

/**** Messages ****/

/*! Some useful strings for sending administrative messages. */
typedef enum {
    MSG_DEV,
    MSG_DEV_MOD,
    MSG_LOGOUT,
    MSG_MAP,
    MSG_MAP_TO,
    MSG_MAPPED,
    MSG_MAP_MOD,
    MSG_NAME_PROBE,
    MSG_NAME_REG,
    MSG_PING,
    MSG_SIG,
    MSG_SIG_REM,
    MSG_SIG_MOD,
    MSG_SUBSCRIBE,
    MSG_SYNC,
    MSG_UNMAP,
    MSG_UNMAPPED,
    MSG_WHO,
    NUM_MSG_STRINGS
} net_msg_t;

/*! Function to call when an allocated resource is locked. */
typedef void mpr_resource_on_lock(struct _mpr_allocated_t *resource);

/*! Function to call when an allocated resource encounters a collision. */
typedef void mpr_resource_on_collision(struct _mpr_allocated_t *resource);

/*! Allocated resources */
typedef struct _mpr_allocated_t {
    double count_time;          //!< The last time collision count was updated.
    double hints[8];            //!< Availability of a range of resource values.

    //!< Function to call when resource becomes locked.
    mpr_resource_on_lock *on_lock;

   //! Function to call when resource collision occurs.
    mpr_resource_on_collision *on_collision;

    unsigned int val;           //!< The resource to be allocated.
    int collision_count;        //!< The number of collisions detected.
    uint8_t locked;             /*!< Whether or not the value has been locked
                                 *   in (allocated). */
    uint8_t online;             /*!< Whether or not we are connected to the
                                 *   distributed allocation network. */
} mpr_allocated_t, *mpr_allocated;

/*! Clock and timing information. */
typedef struct _mpr_sync_time_t {
    lo_timetag time;
    int msg_id;
} mpr_sync_time_t;

typedef struct _mpr_sync_clock_t {
    double rate;
    double offset;
    double latency;
    double jitter;
    mpr_sync_time_t sent;
    mpr_sync_time_t rcvd;
    int new;
} mpr_sync_clock_t, *mpr_sync_clock;

typedef struct _mpr_subscriber {
    struct _mpr_subscriber *next;
    lo_address addr;
    uint32_t lease_exp;
    int flags;
} *mpr_subscriber;

#define TIMEOUT_SEC 10              // timeout after 10 seconds without ping

/**** Object ****/

typedef struct _mpr_obj
{
    mpr_graph graph;                //!< Pointer back to the graph.
    mpr_id id;                      //!< Unique id for this object.
    void *data;                     //!< User context pointer.
    struct _mpr_dict props;         //!< Properties associated with this signal.
    int version;                    //!< Version number.
    mpr_data_type type;             //!< Object type.
} mpr_obj_t, *mpr_obj, **mpr_list;

/**** Signal ****/

/*! A structure that stores the current and historical values of a signal. The
 *  size of the history array is determined by the needs of mapping expressions.
 *  @ingroup signals */

typedef struct _mpr_hist
{
    void *val;                  //!< Value for each sample of stored history.
    mpr_time *time;             //!< Time for each sample of stored history.
    int len;                    //!< Vector length.
    mpr_type type;              //!< The type of this signal.
    char size;                  //!< History size of the buffer.
    char pos;                   //!< Current position in the circular buffer.
} mpr_hist_t, *mpr_hist;

/*! Bit flags for indicating signal instance status. */
#define RELEASED_LOCALLY  0x01
#define RELEASED_REMOTELY 0x02

/*! A signal is defined as a vector of values, along with some metadata. */
typedef struct _mpr_sig_inst
{
    mpr_id id;                  //!< User-assignable instance id.
    void *data;                 //!< User data of this instance.
    mpr_time created;           //!< The instance's creation timestamp.
    char *has_val_flags;        //!< Indicates which vector elements have a value.

    void *val;                  //!< The current value of this signal instance.
    mpr_time time;              //!< The time associated with the current value.

    int idx;                    //!< Index for accessing value history.
    uint8_t has_val;            //!< Indicates whether this instance has a value.
    uint8_t active;             //!< Status of this instance.
} mpr_sig_inst_t, *mpr_sig_inst;

typedef struct _mpr_sig_idmap
{
    struct _mpr_id_map *map;    //!< Associated mpr_id_map.
    struct _mpr_sig_inst *inst; //!< Signal instance.
    int status;                 /*!< Either 0 or a combination of
                                 *   RELEASED_LOCALLY and RELEASED_REMOTELY. */
} mpr_sig_idmap_t;

typedef struct _mpr_local_sig
{
    struct _mpr_dev *dev;           //!< The device associated with this signal.
    struct _mpr_sig_idmap *idmaps;  //!< ID maps and active instances.
    int idmap_len;
    struct _mpr_sig_inst **inst;    //!< Array of pointers to the signal insts.
    char *vec_known;                //!< Bitflags when entire vector is known.

    /*! An optional function to be called when the signal value changes or when
     *  signal instance management events occur.. */
    void *handler;
    int event_flags;                /*! Flags for deciding when to call the
                                     *  instance event handler. */

    mpr_sig_group group;
} mpr_local_sig_t, *mpr_local_sig;

/*! A record that describes properties of a signal. */
struct _mpr_sig {
    mpr_obj_t obj;          // always first
    mpr_local_sig loc;
    mpr_dev dev;
    char *path;             //! OSC path.  Must start with '/'.
    char *name;             //! The name of this signal (path+1).

    char *unit;             //!< The unit of this signal, or NULL for N/A.
    void *min;              //!< The minimum of this signal, or NULL for N/A.
    void *max;              //!< The maximum of this signal, or NULL for N/A.

    float period;           //!< Estimate of the update rate of this signal.
    float jitter;           //!< Estimate of the timing jitter of this signal.

    int dir;                //!< DIR_OUTGOING / DIR_INCOMING / DIR_BOTH
    int len;                //!< Length of the signal vector, or 1 for scalars.
    int num_inst;           //!< Number of instances.
    int use_inst;           //!< 1 if using instances, 0 otherwise.
    int num_maps_in;
    int num_maps_out;

    mpr_type type;              //!< The type of this signal.
    mpr_steal_type steal_mode;  //!< Type of voice stealing to perform.
};

/**** Router ****/

typedef struct _mpr_queue {
    mpr_time time;
    struct {
        lo_bundle udp;
        lo_bundle tcp;
    } bundle;
    struct _mpr_queue *next;
} *mpr_queue;

typedef struct _mpr_link {
    mpr_obj_t obj;                  // always first
    union {
        mpr_dev devs[2];
        struct {
            mpr_dev local_dev;
            mpr_dev remote_dev;
        };
    };
    int *num_maps;

    struct {
        lo_address admin;           //!< Network address of remote endpoint
        lo_address udp;             //!< Network address of remote endpoint
        lo_address tcp;             //!< Network address of remote endpoint
    } addr;

    mpr_queue queues;               /*!< Linked-list of message queues
                                     *   waiting to be sent. */
    mpr_sync_clock_t clock;
} mpr_link_t, *mpr_link;

/**** Maps and Slots ****/

#define MAX_NUM_MAP_SRC     8    // arbitrary
#define MAX_NUM_MAP_DST     8    // arbitrary

typedef struct _mpr_local_slot {
    // each slot can point to local signal or a remote link structure
    struct _mpr_rtr_sig *rsig;      //!< Parent signal if local
    mpr_hist hist;                  /*!< Array of value histories for
                                     *   each signal instance. */
    int hist_size;                  //!< History size.
    char status;
} mpr_local_slot_t, *mpr_local_slot;

typedef struct _mpr_slot {
    mpr_obj_t obj;                  // always first
    mpr_local_slot loc;             //!< Pointer to local resources if any
    struct _mpr_map *map;           //!< Pointer to parent map
    mpr_sig sig;                    //!< Pointer to parent signal
    mpr_link link;

    void *min;                      //!< Array of minima, or NULL for N/A
    void *max;                      //!< Array of maxima, or NULL for N/A
    int num_inst;

    int dir;                        //!< DI_INCOMING or DI_OUTGOING
    int causes_update;              //!< 1 if causes update, 0 otherwise.
    int use_inst;                   //!< 1 if using instances, 0 otherwise.
    int calib;                      //!< >1 if calibrating, 0 otherwise
} mpr_slot_t, *mpr_slot;

/*! The mpr_local_map structure is a linked list of mappings for a given signal.
 *  Each signal can be associated with multiple inputs or outputs. This
 *  structure only contains state information used for performing mapping, the
 *  properties are publically defined in mpr_constants.h. */
typedef struct _mpr_local_map {
    struct _mpr_rtr *rtr;

    mpr_expr expr;                  //!< The mapping expression.
    mpr_hist *expr_var;             //!< User variables values.
    int num_expr_var;               //!< Number of user variables.
    int num_var_inst;

    uint8_t is_local_only;
    uint8_t one_src;
} mpr_local_map_t, *mpr_local_map;

/*! A record that describes the properties of a mapping.
 *  @ingroup map */
typedef struct _mpr_map {
    mpr_obj_t obj;                      // always first
    mpr_local_map loc;
    mpr_slot *src;
    mpr_slot dst;

    mpr_dev *scopes;

    char *expr_str;

    int muted;                          //!< 1 to mute mapping, 0 to unmute
    int num_scopes;
    int num_src;
    mpr_loc process_loc;
    int status;
    int protocol;                       //!< Data transport protocol.
} mpr_map_t, *mpr_map;

/*! The rtr_sig is a linked list containing a signal and a list of mapping
 *  slots.  TODO: This should be replaced with a more efficient approach
 *  such as a hash table or search tree. */
typedef struct _mpr_rtr_sig {
    struct _mpr_rtr_sig *next;          //!< The next rtr_sig in the list.

    struct _mpr_rtr *link;              //!< The parent link.
    struct _mpr_sig *sig;               //!< The associated signal.

    mpr_slot *slots;
    int num_slots;
    int id_counter;

} *mpr_rtr_sig;

/*! The router structure. */
typedef struct _mpr_rtr {
    struct _mpr_dev *dev;        //!< The device associated with this link.
    mpr_rtr_sig sigs;            //!< The list of mappings for each signal.
} mpr_rtr_t, *mpr_rtr;

/*! The instance ID map is a linked list of int32 instance ids for coordinating
 *  remote and local instances. */
typedef struct _mpr_id_map {
    struct _mpr_id_map *next;    //!< The next id map in the list.

    mpr_id GID;                  //!< Hash for originating device.
    mpr_id LID;                  //!< Local instance id to map.
    int LID_refcount;
    int GID_refcount;
} mpr_id_map_t, *mpr_id_map;

/**** Device ****/

typedef struct _mpr_local_dev {
    mpr_allocated_t ordinal;    //!< A unique ordinal for this device instance.
    int registered;             //!< Non-zero if this device has been registered.

    int n_output_callbacks;

    mpr_subscriber subscribers; /*!< Linked-list of subscribed peers. */

    struct {
        struct _mpr_id_map **active;    //!< The list of active instance id maps.
        struct _mpr_id_map *reserve;    //!< The list of reserve instance id maps.
    } idmaps;

    int num_sig_groups;
} mpr_local_dev_t, *mpr_local_dev;


/*! A record that keeps information about a device. */
struct _mpr_dev {
    mpr_obj_t obj;              // always first
    mpr_local_dev loc;

    mpr_dev *linked;

    char *prefix;               //!< The identifier (prefix) for this device.
    char *name;                 //!< The full name for this device, or zero.

    mpr_time synced;            //!< Timestamp of last sync.

    int ordinal;
    int num_inputs;             //!< Number of associated input signals.
    int num_outputs;            //!< Number of associated output signals.
    int num_maps_in;            //!< Number of associated incoming maps.
    int num_maps_out;           //!< Number of associated outgoing maps.
    int num_linked;             //!< Number of linked devices.
    int status;

    uint8_t subscribed;
};

/**** Messages ****/
/* For property indexes, bits 1–8 are used for numberical index, bits 9–14 are
 * used for the mpr_prop enum. */
#define PROP_ADD        0x04000
#define PROP_REMOVE     0x08000
#define DST_SLOT_PROP   0x10000
#define SRC_SLOT_PROP_BIT_OFFSET    17
#define SRC_SLOT_PROP(idx) ((idx + 1) << SRC_SLOT_PROP_BIT_OFFSET)
#define SRC_SLOT(idx) ((idx >> SRC_SLOT_PROP_BIT_OFFSET) - 1)
#define MASK_PROP_BITFLAGS(idx) (idx & 0x3F00)
#define PROP_TO_INDEX(prop) ((prop & 0x3F00) >> 8)
#define INDEX_TO_PROP(idx) (idx << 8)

/* Maximum number of "extra" properties for a signal, device, or map. */
#define NUM_EXTRA_PROPS 20

/*! Queriable representation of a parameterized message parsed from an incoming
 *  OSC message. Does not contain a copy of data, so only valid for the duration
 *  of the message handler. Also allows for a constant number of "extra"
 *  parameters; that is, unknown parameters that may be specified for a signal
 *  and used for metadata, which will be added to a general-purpose string table
 *  associated with the signal. */
typedef struct _mpr_msg_atom
{
    const char *key;
    lo_arg **vals;
    const char *types;
    int len;
    mpr_prop prop;
} mpr_msg_atom_t, *mpr_msg_atom;

typedef struct _mpr_msg
{
    mpr_msg_atom_t *atoms;
    int num_atoms;
} *mpr_msg;

#endif // __MPR_TYPES_H__
