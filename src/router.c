#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>

#include <lo/lo.h>

#include "mapper_internal.h"
#include "types_internal.h"
#include <mapper/mapper.h>

static void send_or_bundle_message(mapper_link link, const char *path,
                                   lo_message msg, mapper_timetag_t tt);

static int map_in_scope(mapper_map map, mapper_id id)
{
    int i;
    id &= 0xFFFFFFFF00000000; // interested in device hash part only
    for (i = 0; i < map->scope.size; i++) {
        if (map->scope.devices[i] == 0 || map->scope.devices[i]->id == id)
            return 1;
    }
    return 0;
}

static void reallocate_slot_instances(mapper_slot slot, int size)
{
    int i;
    if (slot->num_instances < size) {
        slot->local->history = realloc(slot->local->history,
                                       sizeof(struct _mapper_history) * size);
        for (i = slot->num_instances; i < size; i++) {
            slot->local->history[i].type = slot->signal->type;
            slot->local->history[i].length = slot->signal->length;
            slot->local->history[i].size = slot->local->history_size;
            slot->local->history[i].value = calloc(1, mapper_type_size(slot->signal->type)
                                                   * slot->local->history_size);
            slot->local->history[i].timetag = calloc(1, sizeof(mapper_timetag_t)
                                                     * slot->local->history_size);
            slot->local->history[i].position = -1;
        }
        slot->num_instances = size;
    }
}

static void reallocate_map_instances(mapper_map map, int size)
{
    int i, j;
    if (   !(map->status & STATUS_TYPE_KNOWN)
        || !(map->status & STATUS_LENGTH_KNOWN)) {
        for (i = 0; i < map->num_sources; i++) {
            map->sources[i]->num_instances = size;
        }
        map->destination.num_instances = size;
        return;
    }

    // check if source histories need to be reallocated
    for (i = 0; i < map->num_sources; i++)
        reallocate_slot_instances(map->sources[i], size);

    // check if destination histories need to be reallocated
    reallocate_slot_instances(&map->destination, size);

    // check if expression variable histories need to be reallocated
    mapper_map_internal imap = map->local;
    if (size > imap->num_var_instances) {
        imap->expr_vars = realloc(imap->expr_vars, sizeof(mapper_history*) * size);
        for (i = imap->num_var_instances; i < size; i++) {
            imap->expr_vars[i] = malloc(sizeof(struct _mapper_history)
                                        * imap->num_expr_vars);
            for (j = 0; j < imap->num_expr_vars; j++) {
                imap->expr_vars[i][j].type = imap->expr_vars[0][j].type;
                imap->expr_vars[i][j].length = imap->expr_vars[0][j].length;
                imap->expr_vars[i][j].size = imap->expr_vars[0][j].size;
                imap->expr_vars[i][j].position = -1;
                imap->expr_vars[i][j].value = calloc(1, sizeof(double)
                                                     * imap->expr_vars[i][j].length
                                                     * imap->expr_vars[i][j].size);
                imap->expr_vars[i][j].timetag = calloc(1, sizeof(mapper_timetag_t)
                                                       * imap->expr_vars[i][j].size);
            }
        }
        imap->num_var_instances = size;
    }
}

// TODO: check for mismatched instance counts when using multiple sources
void mapper_router_num_instances_changed(mapper_router rtr, mapper_signal sig,
                                         int size)
{
    int i;
    // check if we have a reference to this signal
    mapper_router_signal rs = rtr->signals;
    while (rs) {
        if (rs->signal == sig)
            break;
        rs = rs->next;
    }

    if (!rs) {
        // The signal is not mapped through this router.
        return;
    }

    // for array of slots, may need to reallocate destination instances
    for (i = 0; i < rs->num_slots; i++) {
        mapper_slot slot = rs->slots[i];
        if (slot)
            reallocate_map_instances(slot->map, size);
    }
}

void mapper_router_process_signal(mapper_router rtr, mapper_signal sig,
                                  int instance, const void *value, int count,
                                  mapper_timetag_t tt)
{
    mapper_id_map id_map = sig->local->id_maps[instance].map;
    lo_message msg;

    // find the router signal
    mapper_router_signal rs = rtr->signals;
    while (rs) {
        if (rs->signal == sig)
            break;
        rs = rs->next;
    }
    if (!rs)
        return;

    int i, j, k, id = sig->local->id_maps[instance].instance->index;
    mapper_map map;
    mapper_map_internal imap;

    if (!value) {
        mapper_slot_internal islot;
        mapper_slot slot;

        for (i = 0; i < rs->num_slots; i++) {
            if (!rs->slots[i])
                continue;

            slot = rs->slots[i];
            map = slot->map;
            imap = map->local;

            if (map->status < STATUS_ACTIVE)
                continue;

            // need to reset user variable memory for this instance
            for (j = 0; j < imap->num_expr_vars; j++) {
                memset(imap->expr_vars[id][j].value, 0, sizeof(double)
                       * imap->expr_vars[id][j].length
                       * imap->expr_vars[id][j].size);
                memset(imap->expr_vars[id][j].timetag, 0,
                       sizeof(mapper_timetag_t)
                       * imap->expr_vars[id][j].size);
                imap->expr_vars[id][j].position = -1;
            }

            mapper_slot dst_slot = &map->destination;
            mapper_slot_internal dst_islot = dst_slot->local;

            // also need to reset associated output memory
            dst_islot->history[id].position= -1;
            memset(dst_islot->history[id].value, 0, dst_islot->history_size
                   * dst_slot->signal->length * mapper_type_size(dst_slot->signal->type));
            memset(dst_islot->history[id].timetag, 0, dst_islot->history_size
                   * sizeof(mapper_timetag_t));
            dst_islot->history[id].position = -1;

            if (slot->direction == MAPPER_DIR_OUTGOING
                && !(sig->local->id_maps[instance].status & RELEASED_REMOTELY)) {
                msg = 0;
                if (!slot->use_as_instance)
                    msg = mapper_map_build_message(map, slot, 0, 1, 0, 0);
                else if (map_in_scope(map, id_map->global))
                    msg = mapper_map_build_message(map, slot, 0, 1, 0, id_map);
                if (msg)
                    send_or_bundle_message(dst_slot->link,
                                           dst_slot->signal->path, msg, tt);
            }

            for (j = 0; j < map->num_sources; j++) {
                slot = map->sources[j];
                islot = slot->local;

                // also need to reset associated input memory
                memset(islot->history[id].value, 0, islot->history_size
                       * slot->signal->length * mapper_type_size(slot->signal->type));
                memset(islot->history[id].timetag, 0, islot->history_size
                       * sizeof(mapper_timetag_t));
                islot->history[id].position = -1;

                if (!map->sources[j]->use_as_instance)
                    continue;

                if (!map_in_scope(map, id_map->global))
                    continue;

                if (sig->local->id_maps[instance].status & RELEASED_REMOTELY)
                    continue;

                if (slot->direction == MAPPER_DIR_INCOMING) {
                    // send release to upstream
                    msg = mapper_map_build_message(map, slot, 0, 1, 0, id_map);
                    if (msg)
                        send_or_bundle_message(slot->link, slot->signal->path,
                                               msg, tt);
                }
            }
        }
        return;
    }

    // if count > 1, we need to allocate sufficient memory for largest output
    // vector so that we can store calculated values before sending
    // TODO: calculate max_output_size, cache in link_signal
    void *out_value_p = count == 1 ? 0 : alloca(count * sig->length
                                                * sizeof(double));
    for (i = 0; i < rs->num_slots; i++) {
        if (!rs->slots[i])
            continue;

        mapper_slot slot = rs->slots[i];
        map = slot->map;

        if (map->status < STATUS_ACTIVE)
            continue;

        int in_scope = map_in_scope(map, id_map->global);
        // TODO: should we continue for out-of-scope local destination updates?
        if (slot->use_as_instance && !in_scope) {
            continue;
        }

        mapper_slot_internal islot = slot->local;
        mapper_slot dst_slot = &map->destination;
        mapper_slot to = (map->process_location == MAPPER_LOC_SOURCE ? dst_slot : slot);
        int to_size = mapper_type_size(to->signal->type) * to->signal->length;
        char src_types[slot->signal->length * count];
        memset(src_types, slot->signal->type, slot->signal->length * count);
        char dst_types[to->signal->length * count];
        memset(dst_types, to->signal->type, to->signal->length * count);
        k = 0;
        for (j = 0; j < count; j++) {
            // copy input history
            size_t n = mapper_signal_vector_bytes(sig);
            islot->history[id].position = ((islot->history[id].position + 1)
                                           % islot->history[id].size);
            memcpy(mapper_history_value_ptr(islot->history[id]), value + n * j, n);
            memcpy(mapper_history_tt_ptr(islot->history[id]),
                   &tt, sizeof(mapper_timetag_t));

            // process source boundary behaviour
            if ((mapper_boundary_perform(&islot->history[id], slot,
                                         src_types + slot->signal->length * k))) {
                // back up position index
                --islot->history[id].position;
                if (islot->history[id].position < 0)
                    islot->history[id].position = islot->history[id].size - 1;
                continue;
            }

            if (slot->direction == MAPPER_DIR_INCOMING) {
                continue;
            }

            if (map->process_location == MAPPER_LOC_SOURCE && !slot->causes_update)
                continue;

            if (!(mapper_map_perform(map, slot, id,
                                     dst_types + to->signal->length * k)))
                continue;

            if (map->process_location == MAPPER_LOC_SOURCE) {
                // also process destination boundary behaviour
                if ((mapper_boundary_perform(&map->destination.local->history[id],
                                             dst_slot,
                                             dst_types + to->signal->length * k))) {
                    // back up position index
                    --map->destination.local->history[id].position;
                    if (map->destination.local->history[id].position < 0)
                        map->destination.local->history[id].position = map->destination.local->history[id].size - 1;
                    continue;
                }
            }

            void *result = mapper_history_value_ptr(map->destination.local->history[id]);

            if (count > 1) {
                memcpy((char*)out_value_p + to_size * j, result, to_size);
            }
            else {
                msg = mapper_map_build_message(map, slot, result, 1, dst_types,
                                               slot->use_as_instance ? id_map : 0);
                if (msg)
                    send_or_bundle_message(map->destination.link,
                                           dst_slot->signal->path, msg, tt);
            }
            ++k;
        }
        if (count > 1 && slot->direction == MAPPER_DIR_OUTGOING
            && (!slot->use_as_instance || in_scope)) {
            msg = mapper_map_build_message(map, slot, out_value_p, k, dst_types,
                                           slot->use_as_instance ? id_map : 0);
            if (msg)
                send_or_bundle_message(map->destination.link,
                                       dst_slot->signal->path, msg, tt);
        }
    }
}

int mapper_router_send_query(mapper_router rtr, mapper_signal sig,
                             mapper_timetag_t tt)
{
    if (!sig->local->update_handler) {
        trace("not sending queries since signal has no handler.\n");
        return 0;
    }
    // find the corresponding router_signal
    mapper_router_signal rs = rtr->signals;
    while (rs && rs->signal != sig)
        rs = rs->next;

    // exit without failure if signal is not mapped
    if (!rs)
        return 0;

    // for each map, query remote signals
    int i, j, count = 0;
    char query_string[256];
    for (i = 0; i < rs->num_slots; i++) {
        if (!rs->slots[i])
            continue;
        mapper_map map = rs->slots[i]->map;
        if (map->status != STATUS_ACTIVE)
            continue;
        lo_message msg = lo_message_new();
        if (!msg)
            continue;
        lo_message_add_string(msg, sig->path);
        lo_message_add_int32(msg, sig->length);
        lo_message_add_char(msg, sig->type);
        // TODO: include response address as argument to allow TCP queries?
        // TODO: always use TCP for queries?

        if (rs->slots[i]->direction == MAPPER_DIR_OUTGOING) {
            snprintf(query_string, 256, "%s/get", map->destination.signal->path);
            send_or_bundle_message(map->destination.link, query_string, msg, tt);
        }
        else {
            for (j = 0; j < map->num_sources; j++) {
                snprintf(query_string, 256, "%s/get", map->sources[j]->signal->path);
                send_or_bundle_message(map->sources[j]->link, query_string, msg, tt);
            }
        }
        ++count;
    }
    return count;
}

// note on memory handling of mapper_router_bundle_message():
// path: not owned, will not be freed (assumed is signal name, owned by signal)
// message: will be owned, will be freed when done
void send_or_bundle_message(mapper_link link, const char *path, lo_message msg,
                            mapper_timetag_t tt)
{
    mapper_link_internal ilink = link->local;
    // Check if a matching bundle exists
    mapper_queue q = ilink->queues;
    while (q) {
        if (memcmp(&q->tt, &tt,
                   sizeof(mapper_timetag_t))==0)
            break;
        q = q->next;
    }
    if (q) {
        // Add message to existing bundle
        lo_bundle_add_message(q->bundle, path, msg);
    }
    else {
        // Send message immediately
        lo_bundle b = lo_bundle_new(tt);
        lo_bundle_add_message(b, path, msg);
        lo_send_bundle_from(ilink->data_addr, link->local_device->local->server, b);
        lo_bundle_free_messages(b);
    }
}

static mapper_router_signal find_or_add_router_signal(mapper_router rtr,
                                                      mapper_signal sig)
{
    // find signal in router_signal list
    mapper_router_signal rs = rtr->signals;
    while (rs && rs->signal != sig)
        rs = rs->next;

    // if not found, create a new list entry
    if (!rs) {
        rs = ((mapper_router_signal)
              calloc(1, sizeof(struct _mapper_router_signal)));
        rs->signal = sig;
        rs->num_slots = 1;
        rs->slots = malloc(sizeof(mapper_slot_internal *));
        rs->slots[0] = 0;
        rs->next = rtr->signals;
        rtr->signals = rs;
    }
    return rs;
}

static int router_signal_store_slot(mapper_router_signal rs, mapper_slot slot)
{
    int i;
    for (i = 0; i < rs->num_slots; i++) {
        if (!rs->slots[i]) {
            // store pointer at empty index
            rs->slots[i] = slot;
            return i;
        }
    }
    // all indices occupied, allocate more
    rs->slots = realloc(rs->slots, sizeof(mapper_slot*) * rs->num_slots * 2);
    rs->slots[rs->num_slots] = slot;
    for (i = rs->num_slots+1; i < rs->num_slots * 2; i++) {
        rs->slots[i] = 0;
    }
    i = rs->num_slots;
    rs->num_slots *= 2;
    return i;
}

static mapper_id unused_map_id(mapper_device dev, mapper_router rtr)
{
    int i, done = 0;
    mapper_id id;
    while (!done) {
        done = 1;
        id = mapper_device_unique_id(dev);
        // check if map exists with this id
        mapper_router_signal rs = rtr->signals;
        while (rs) {
            for (i = 0; i < rs->num_slots; i++) {
                if (!rs->slots[i])
                    continue;
                if (rs->slots[i]->map->id == id) {
                    done = 0;
                    break;
                }
            }
            rs = rs->next;
        }
    }
    return id;
}

static void alloc_and_init_local_slot(mapper_router rtr, mapper_slot slot,
                                      int is_src, int *max_instances)
{
    slot->direction = ((is_src ^ (slot->signal->local ? 1 : 0))
                       ? MAPPER_DIR_INCOMING : MAPPER_DIR_OUTGOING);

    slot->local = ((mapper_slot_internal)
                   calloc(1, sizeof(struct _mapper_slot_internal)));
    if (slot->signal->local) {
        slot->local->router_sig = find_or_add_router_signal(rtr, slot->signal);
        router_signal_store_slot(slot->local->router_sig, slot);

        if (slot->signal->num_instances > *max_instances)
            *max_instances = slot->signal->num_instances;

        slot->local->status |= STATUS_TYPE_KNOWN;
        slot->local->status |= STATUS_LENGTH_KNOWN;

        // start with signal extrema if known
        if (!slot->maximum && slot->signal->maximum) {
            // copy range from signal
            slot->maximum = malloc(mapper_signal_vector_bytes(slot->signal));
            memcpy(slot->maximum, slot->signal->maximum,
                   mapper_signal_vector_bytes(slot->signal));
        }
        if (!slot->minimum && slot->signal->minimum) {
            // copy range from signal
            slot->minimum = malloc(mapper_signal_vector_bytes(slot->signal));
            memcpy(slot->minimum, slot->signal->minimum,
                   mapper_signal_vector_bytes(slot->signal));
        }
    }
    if (!slot->signal->local || (is_src && slot->map->destination.signal->local)) {
        mapper_link link;
        link = mapper_device_link_by_remote_device(rtr->device,
                                                   slot->signal->device);
        if (!link) {
            link = mapper_database_add_or_update_link(rtr->device->database,
                                                      rtr->device, slot->signal->device,
                                                      0);
        }
        mapper_link_init(link, 1);
        slot->link = link;
    }

    // set some sensible defaults
    slot->calibrating = 0;
    slot->causes_update = 1;
}

void mapper_router_add_map(mapper_router rtr, mapper_map map)
{
    int i;
    int local_dst = map->destination.signal->local ? 1 : 0;
    int local_src = 0;
    for (i = 0; i < map->num_sources; i++) {
        if (map->sources[i]->signal->local)
            ++local_src;
    }

    if (map->local) {
        trace("error in mapper_router_add_map – local structures already exist.\n");
        return;
    }

    mapper_map_internal imap = ((mapper_map_internal)
                                calloc(1, sizeof(struct _mapper_map_internal)));
    map->local = imap;
    imap->router = rtr;

    // TODO: configure number of instances available for each slot
    imap->num_var_instances = 0;

    // Allocate local slot structures
    int max_num_instances = 0;
    for (i = 0; i < map->num_sources; i++)
        alloc_and_init_local_slot(rtr, map->sources[i], 1, &max_num_instances);
    alloc_and_init_local_slot(rtr, &map->destination, 0, &max_num_instances);

    // Set num_instances property
    if (map->destination.signal->local)
        map->destination.num_instances = map->destination.signal->num_instances;
    else
        map->destination.num_instances = max_num_instances;
    map->destination.use_as_instance = map->destination.num_instances > 1;
    for (i = 0; i < map->num_sources; i++) {
        if (map->sources[i]->signal->local)
            map->sources[i]->num_instances = map->sources[i]->signal->num_instances;
        else
            map->sources[i]->num_instances = map->destination.num_instances;
        map->sources[i]->use_as_instance = map->sources[i]->num_instances > 1;
    }
    imap->num_var_instances = max_num_instances;

    // assign a unique id to this map if we are the destination
    if (local_dst)
        map->id = unused_map_id(rtr->device, rtr);

    /* assign indices to source slots - may be overwritten later by message */
    for (i = 0; i < map->num_sources; i++) {
        map->sources[i]->id = i;
    }
    map->destination.id = (local_dst
                           ? map->destination.local->router_sig->id_counter++ : -1);

    // add scopes
    int scope_count = 0;
    map->scope.size = map->num_sources;
    map->scope.devices = (mapper_device *) malloc(sizeof(mapper_device)
                                                  * map->scope.size);

    for (i = 0; i < map->num_sources; i++) {
        // check that scope has not already been added
        int j, found = 0;
        for (j = 0; j < scope_count; j++) {
            if (map->scope.devices[j] == map->sources[i]->signal->device) {
                found = 1;
                break;
            }
        }
        if (!found) {
            map->scope.devices[scope_count] = map->sources[i]->signal->device;
            ++scope_count;
        }
    }

    if (scope_count != map->num_sources) {
        map->scope.size = scope_count;
        map->scope.devices = realloc(map->scope.devices,
                                     sizeof(mapper_device) * scope_count);
    }

    // check if all sources belong to same remote device
    imap->one_source = 1;
    for (i = 1; i < map->num_sources; i++) {
        if (map->sources[i]->link != map->sources[0]->link) {
            imap->one_source = 0;
            break;
        }
    }

    // default to processing at source device unless heterogeneous sources
    if (imap->one_source)
        map->process_location = MAPPER_LOC_SOURCE;
    else
        map->process_location = MAPPER_LOC_DESTINATION;

    if (local_dst && (local_src == map->num_sources)) {
        // all reference signals are local
        imap->is_local = 1;
        map->destination.link = map->sources[0]->link;
    }
}

static void check_link(mapper_router rtr, mapper_link link)
{
    /* We could remove link the link here if it has no associated maps, however
     * under normal usage it is likely that users will add new maps after
     * deleting the old ones. If we remove the link immediately it will have to
     * be re-established in this scenario, so we will allow the house-keeping
     * routines to clean up empty links after the link ping timeout period. */
}

void mapper_router_remove_signal(mapper_router rtr, mapper_router_signal rs)
{
    if (rtr && rs) {
        // No maps remaining – we can remove the router_signal also
        mapper_router_signal *rstemp = &rtr->signals;
        while (*rstemp) {
            if (*rstemp == rs) {
                *rstemp = rs->next;
                free(rs->slots);
                free(rs);
                break;
            }
            rstemp = &(*rstemp)->next;
        }
    }
}

static void free_slot_memory(mapper_slot slot)
{
    int i;
    if (!slot->local)
        return;
    // TODO: use router_signal for holding memory of local slots for effiency
//    if (!slot->local->router_sig) {
        if (slot->local->history) {
            for (i = 0; i < slot->num_instances; i++) {
                free(slot->local->history[i].value);
                free(slot->local->history[i].timetag);
            }
            free(slot->local->history);
        }
//    }
    free(slot->local);
}

int mapper_router_remove_map(mapper_router rtr, mapper_map map)
{
    // do not free local names since they point to signal's copy
    int i, j;
    if (!map || !map->local)
        return 1;

    // remove map and slots from router_signal lists if necessary
    if (map->destination.local->router_sig) {
        mapper_router_signal rs = map->destination.local->router_sig;
        for (i = 0; i < rs->num_slots; i++) {
            if (rs->slots[i] == &map->destination) {
                rs->slots[i] = 0;
                break;
            }
        }
    }
    else if (map->status >= STATUS_READY && map->destination.link) {
        --map->destination.link->num_maps[0];
        check_link(rtr, map->destination.link);
    }
    free_slot_memory(&map->destination);
    for (i = 0; i < map->num_sources; i++) {
        if (map->sources[i]->local->router_sig) {
            mapper_router_signal rs = map->sources[i]->local->router_sig;
            for (j = 0; j < rs->num_slots; j++) {
                if (rs->slots[j] == map->sources[i]) {
                    rs->slots[j] = 0;
                }
            }
        }
        else if (map->status >= STATUS_READY && map->sources[i]->link) {
            --map->sources[i]->link->num_maps[1];
            check_link(rtr, map->sources[i]->link);
        }
        free_slot_memory(map->sources[i]);
    }

    // free buffers associated with user-defined expression variables
    if (map->local->expr_vars) {
        for (i = 0; i < map->local->num_var_instances; i++) {
            if (map->local->num_expr_vars) {
                for (j = 0; j < map->local->num_expr_vars; j++) {
                    free(map->local->expr_vars[i][j].value);
                    free(map->local->expr_vars[i][j].timetag);
                }
            }
            free(map->local->expr_vars[i]);
        }
        free(map->local->expr_vars);
    }
    if (map->local->expr)
        mapper_expr_free(map->local->expr);

    free(map->local);
    return 0;
}

static int match_slot(mapper_device dev, mapper_slot slot, const char *full_name)
{
    if (!full_name)
        return 1;
    full_name += (full_name[0]=='/');
    const char *sig_name = strchr(full_name+1, '/');
    if (!sig_name)
        return 1;
    int len = sig_name - full_name;
    const char *slot_devname = (slot->local->router_sig ? mapper_device_name(dev)
                                : slot->link->remote_device->name);

    // first compare device name
    if (strlen(slot_devname) != len || strncmp(full_name, slot_devname, len))
        return 1;

    if (strcmp(sig_name+1, slot->signal->name) == 0)
        return 0;
    return 1;
}

mapper_map mapper_router_find_outgoing_map(mapper_router rtr,
                                           mapper_signal local_src,
                                           int num_sources,
                                           const char **src_names,
                                           const char *dest_name)
{
    // find associated router_signal
    mapper_router_signal rs = rtr->signals;
    while (rs && rs->signal != local_src)
        rs = rs->next;
    if (!rs)
        return 0;

    // find associated map
    int i, j;
    for (i = 0; i < rs->num_slots; i++) {
        if (!rs->slots[i] || rs->slots[i]->direction == MAPPER_DIR_INCOMING)
            continue;
        mapper_slot slot = rs->slots[i];
        mapper_map map = slot->map;

        // check destination
        if (match_slot(rtr->device, &map->destination, dest_name))
            continue;

        // check sources
        int found = 1;
        for (j = 0; j < map->num_sources; j++) {
            if (map->sources[j]->local->router_sig == rs)
                continue;

            if (match_slot(rtr->device, map->sources[j], src_names[j])) {
                found = 0;
                break;
            }
        }
        if (found)
            return map;
    }
    return 0;
}

mapper_map mapper_router_find_incoming_map(mapper_router rtr,
                                           mapper_signal local_dst,
                                           int num_sources,
                                           const char **src_names)
{
    // find associated router_signal
    mapper_router_signal rs = rtr->signals;
    while (rs && rs->signal != local_dst)
        rs = rs->next;
    if (!rs)
        return 0;

    // find associated map
    int i, j;
    for (i = 0; i < rs->num_slots; i++) {
        if (!rs->slots[i] || rs->slots[i]->direction == MAPPER_DIR_OUTGOING)
            continue;
        mapper_map map = rs->slots[i]->map;

        // check sources
        int found = 1;
        for (j = 0; j < num_sources; j++) {
            if (match_slot(rtr->device, map->sources[j], src_names[j])) {
                found = 0;
                break;
            }
        }
        if (found)
            return map;
    }
    return 0;
}

mapper_map mapper_router_find_incoming_map_by_id(mapper_router rtr,
                                                 mapper_signal local_dst,
                                                 mapper_id id)
{
    mapper_router_signal rs = rtr->signals;
    while (rs && rs->signal != local_dst)
        rs = rs->next;
    if (!rs)
        return 0;

    int i;
    for (i = 0; i < rs->num_slots; i++) {
        if (!rs->slots[i] || rs->slots[i]->direction == MAPPER_DIR_OUTGOING)
            continue;
        if (rs->slots[i]->map->id == id)
            return rs->slots[i]->map;
    }
    return 0;
}

mapper_map mapper_router_find_outgoing_map_by_id(mapper_router router,
                                                 mapper_signal local_src,
                                                 mapper_id id)
{
    int i;
    mapper_router_signal rs = router->signals;
    while (rs && rs->signal != local_src)
        rs = rs->next;
    if (!rs)
        return 0;

    for (i = 0; i < rs->num_slots; i++) {
        if (!rs->slots[i] || rs->slots[i]->direction == MAPPER_DIR_INCOMING)
            continue;
        if (rs->slots[i]->map->id == id)
            return rs->slots[i]->map;
    }
    return 0;
}

mapper_slot mapper_router_find_slot(mapper_router router, mapper_signal signal,
                                    int slot_id)
{
    // only interested in incoming slots
    mapper_router_signal rs = router->signals;
    while (rs && rs->signal != signal)
        rs = rs->next;
    if (!rs)
        return NULL; // no associated router_signal

    int i, j;
    mapper_map map;
    for (i = 0; i < rs->num_slots; i++) {
        if (!rs->slots[i] || rs->slots[i]->direction == MAPPER_DIR_OUTGOING)
            continue;
        map = rs->slots[i]->map;
        // check incoming slots for this map
        for (j = 0; j < map->num_sources; j++) {
            if (map->sources[j]->id == slot_id)
                return map->sources[j];
        }
    }
    return NULL;
}
