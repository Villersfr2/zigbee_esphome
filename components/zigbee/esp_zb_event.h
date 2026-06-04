#pragma once

#ifdef USE_ESP32

#include <cstddef>  // for offsetof
#include <cstring>  // for memcpy
#include "esp_zigbee.h"
#include "ezbee/zha.h"

namespace esphome::zigbee {

class ZBEvent {
 public:
  ZBEvent(ezb_zcl_message_info_t info, ezb_zcl_attribute_t attribute, uint8_t *current_level) {
    this->callback_id_ = EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID;
    this->init_set_attr_value_data(info, attribute, current_level);
  }

  ZBEvent(const ezb_zcl_cmd_report_attr_message_t *message) {
    this->callback_id_ = EZB_ZCL_CORE_REPORT_ATTR_CB_ID;
    this->init_report_attr_data(message);
  }

  ZBEvent(ezb_zcl_message_info_t info, ezb_zcl_read_attr_rsp_variable_t *variables) {
    this->callback_id_ = EZB_ZCL_CORE_READ_ATTR_RSP_CB_ID;
    this->init_read_attr_resp_data(info, variables);
  }

  ~ZBEvent() { this->release(); }

  ZBEvent() : event_{}, callback_id_(EZB_ZCL_CORE_BASIC_RESET_TO_FACTORY_DEFAULT_CB_ID) {}

  void release() {
    // Free any allocated memory within the event
    switch (this->callback_id_) {
      case EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID:
        if (this->event_.set_attr.attribute.data.value != nullptr &&
            this->event_.set_attr.attribute.data.value != this->event_.set_attr.inline_data) {
          free(this->event_.set_attr.attribute.data.value);
          this->event_.set_attr.attribute.data.value = nullptr;
        }
        break;
      case EZB_ZCL_CORE_REPORT_ATTR_CB_ID:
        if (this->event_.report_attr.variables.attr_value != nullptr &&
            this->event_.report_attr.variables.attr_value != this->event_.report_attr.inline_data) {
          free(this->event_.report_attr.variables.attr_value);
          this->event_.report_attr.variables.attr_value = nullptr;
        }
        break;
      case EZB_ZCL_CORE_READ_ATTR_RSP_CB_ID: {
        ezb_zcl_read_attr_rsp_variable_t *var = &(this->event_.read_attr_resp.variables);
        while (var != nullptr) {
          if (var->attr_value != nullptr && var->attr_value != this->event_.read_attr_resp.inline_data) {
            free(var->attr_value);
            var->attr_value = nullptr;
          }
          var = var->next;
        }
      }
        // Reset the event data
        this->callback_id_ = EZB_ZCL_CORE_BASIC_RESET_TO_FACTORY_DEFAULT_CB_ID;  // or some invalid value
        break;
      default:
        break;
    }
  }

  void load_set_attr_value_event(ezb_zcl_message_info_t info, ezb_zcl_attribute_t attribute, uint8_t *current_level) {
    this->release();
    this->callback_id_ = EZB_ZCL_CORE_SET_ATTR_VALUE_CB_ID;
    this->init_set_attr_value_data(info, attribute, current_level);
  }

  void load_report_attr_event(const ezb_zcl_cmd_report_attr_message_t *message) {
    this->release();
    this->callback_id_ = EZB_ZCL_CORE_REPORT_ATTR_CB_ID;
    this->init_report_attr_data(message);
  }

  void load_read_attr_resp_event(ezb_zcl_message_info_t info, ezb_zcl_read_attr_rsp_variable_t *variables) {
    this->release();
    this->callback_id_ = EZB_ZCL_CORE_READ_ATTR_RSP_CB_ID;
    this->init_read_attr_resp_data(info, variables);
  }

  // Disable copy to prevent double-delete
  ZBEvent(const ZBEvent &) = delete;
  ZBEvent &operator=(const ZBEvent &) = delete;

  union {
    struct set_attr_event {
      ezb_zcl_message_info_t info;
      ezb_zcl_attribute_t attribute;
      uint8_t current_level;
      bool has_current_level;
      uint8_t inline_data[4];  // For small data types
    } set_attr;
    struct report_attr_event {
      uint8_t dst_endpoint;
      uint16_t cluster;
      ezb_zcl_report_attr_variable_t variables;
      ezb_address_t src_address;
      uint8_t src_endpoint;
      uint8_t inline_data[4];  // For small data types
    } report_attr;
    struct read_attr_resp_event {
      ezb_zcl_message_info_t info;
      ezb_zcl_read_attr_rsp_variable_t variables;
      uint8_t inline_data[4];  // For small data types
    } read_attr_resp;
  } event_;

  ezb_zcl_core_action_callback_id_t callback_id_;

 private:
  void init_set_attr_value_data(ezb_zcl_message_info_t info, ezb_zcl_attribute_t attribute, uint8_t *current_level) {
    this->event_.set_attr.info = info;
    this->event_.set_attr.attribute = attribute;
    // get attribute.data.value with correct type
    this->event_.set_attr.has_current_level = (current_level != nullptr);
    if (current_level != nullptr) {
      this->event_.set_attr.current_level = *current_level;
    }
    if (attribute.data.value != nullptr) {
      // Copy the attribute value to avoid dangling pointer issues
      size_t value_size = ezb_zcl_get_attr_value_size(attribute.data.type, attribute.data.value);
      if (value_size > 4) {
        this->event_.set_attr.attribute.data.value = malloc(value_size);
        if (this->event_.set_attr.attribute.data.value != nullptr) {
          memcpy(this->event_.set_attr.attribute.data.value, attribute.data.value, value_size);
        }
      } else {
        memcpy(this->event_.set_attr.inline_data, attribute.data.value, value_size);
        this->event_.set_attr.attribute.data.value = this->event_.set_attr.inline_data;
      }
    }
  }

  void init_report_attr_data(const ezb_zcl_cmd_report_attr_message_t *message) {
    this->event_.report_attr.dst_endpoint = message->in.header->dst_ep;
    this->event_.report_attr.cluster = message->in.header->cluster_id;
    this->event_.report_attr.variables.attr_id = message->in.variables->attr_id;
    this->event_.report_attr.variables.attr_type = message->in.variables->attr_type;
    this->event_.report_attr.src_address = message->in.header->src_addr;
    this->event_.report_attr.src_endpoint = message->in.header->src_ep;
    if (message->in.variables->attr_value != nullptr) {
      // Copy the attribute value to avoid dangling pointer issues
      size_t value_size =
          ezb_zcl_get_attr_value_size(message->in.variables->attr_type, message->in.variables->attr_value);
      if (value_size > 4) {
        this->event_.report_attr.variables.attr_value = malloc(value_size);
        if (this->event_.report_attr.variables.attr_value != nullptr) {
          memcpy(this->event_.report_attr.variables.attr_value, message->in.variables->attr_value, value_size);
        }
      } else {
        memcpy(this->event_.report_attr.inline_data, message->in.variables->attr_value, value_size);
        this->event_.report_attr.variables.attr_value = this->event_.report_attr.inline_data;
      }
    }
    ezb_zcl_report_attr_variable_t *var = message->in.variables;
    ezb_zcl_report_attr_variable_t *var_last = &(this->event_.report_attr.variables);
    while (var->next != nullptr) {
      var = var->next;
      size_t value_size = ezb_zcl_get_attr_value_size(var->attr_type, var->attr_value);
      var_last->next = new ezb_zcl_report_attr_variable_t();
      if (var_last->next != nullptr) {
        var_last->next->attr_id = var->attr_id;
        var_last->next->attr_type = var->attr_type;
        var_last->next->attr_value = malloc(value_size);
        if (var_last->next->attr_value != nullptr) {
          memcpy(var_last->next->attr_value, var->attr_value, value_size);
        }
        var_last = var_last->next;
      }
    }
  }

  void init_read_attr_resp_data(ezb_zcl_message_info_t info, ezb_zcl_read_attr_rsp_variable_t *variables) {
    this->event_.read_attr_resp.info = info;
    if (variables == nullptr) {
      return;
    }
    this->event_.read_attr_resp.variables.status = variables->status;
    this->event_.read_attr_resp.variables.attr_id = variables->attr_id;
    this->event_.read_attr_resp.variables.attr_type = variables->attr_type;
    //  Note: variables is a pointer to a struct/list; deep copy needed
    if (variables->attr_value != nullptr) {
      size_t value_size = ezb_zcl_get_attr_value_size(variables->attr_type, variables->attr_value);
      if (value_size > 4) {
        this->event_.read_attr_resp.variables.attr_value = malloc(value_size);
        if (this->event_.read_attr_resp.variables.attr_value != nullptr) {
          memcpy(this->event_.read_attr_resp.variables.attr_value, variables->attr_value, value_size);
        }
      } else {
        memcpy(this->event_.read_attr_resp.inline_data, variables->attr_value, value_size);
        this->event_.read_attr_resp.variables.attr_value = this->event_.read_attr_resp.inline_data;
      }
    }
    ezb_zcl_read_attr_rsp_variable_t *var = variables;
    ezb_zcl_read_attr_rsp_variable_t *var_last = &(this->event_.read_attr_resp.variables);
    while (var->next != nullptr) {
      var = var->next;
      size_t value_size = ezb_zcl_get_attr_value_size(var->attr_type, var->attr_value);
      var_last->next = new ezb_zcl_read_attr_rsp_variable_t();
      if (var_last->next != nullptr) {
        var_last->next->attr_id = var->attr_id;
        var_last->next->status = var->status;
        var_last->next->attr_type = var->attr_type;
        var_last->next->attr_value = malloc(value_size);
        if (var_last->next->attr_value != nullptr) {
          memcpy(var_last->next->attr_value, var->attr_value, value_size);
        }
        var_last = var_last->next;
      }
    }
  }
};
}  // namespace esphome::zigbee
#endif  // USE_ESP32