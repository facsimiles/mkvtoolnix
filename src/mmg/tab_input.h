/*
  mkvmerge GUI -- utility for splicing together matroska files
      from component media subtypes

  tab_input.h

  Written by Moritz Bunkus <moritz@bunkus.org>
  Parts of this code were written by Florian Wager <root@sirelvis.de>

  Distributed under the GPL
  see the file COPYING for details
  or visit http://www.gnu.org/copyleft/gpl.html
*/

/*!
    \file
    \version $Id$
    \brief declarations for the input tab
    \author Moritz Bunkus <moritz@bunkus.org>
*/

#ifndef __TAB_INPUT_H
#define __TAB_INPUT_H

#include "os.h"

#include "wx/config.h"

#define ID_LB_INPUTFILES                  11000
#define ID_B_ADDFILE                      11001
#define ID_B_REMOVEFILE                   11002
#define ID_CLB_TRACKS                     11003
#define ID_CB_LANGUAGE                    11004
#define ID_TC_TRACKNAME                   11005
#define ID_CB_CUES                        11006
#define ID_CB_MAKEDEFAULT                 11007
#define ID_TC_DELAY                       11008
#define ID_TC_STRETCH                     11009
#define ID_CB_NOCHAPTERS                  11010
#define ID_CB_SUBTITLECHARSET             11011
#define ID_CB_AACISSBR                    11012
#define ID_TC_TAGS                        11013
#define ID_B_BROWSETAGS                   11014
#define ID_CB_ASPECTRATIO                 11015
#define ID_CB_FOURCC                      11016
#define ID_CB_NOATTACHMENTS               11017
#define ID_CB_NOTAGS                      11018
#define ID_CB_COMPRESSION                 11019
#define ID_TC_TIMECODES                   11020
#define ID_B_BROWSE_TIMECODES             11021
#define ID_RB_ASPECTRATIO                 11022
#define ID_RB_DISPLAYDIMENSIONS           11023
#define ID_TC_DISPLAYWIDTH                11024
#define ID_TC_DISPLAYHEIGHT               11025
#define ID_B_INPUTUP                      11026
#define ID_B_INPUTDOWN                    11027
#define ID_B_TRACKUP                      11028
#define ID_B_TRACKDOWN                    11029
#define ID_T_INPUTVALUES                  11030

class tab_input: public wxPanel {
  DECLARE_CLASS(tab_input);
  DECLARE_EVENT_TABLE();
protected:
  wxListBox *lb_input_files;
  wxButton *b_add_file, *b_remove_file, *b_browse_tags, *b_browse_timecodes;
  wxButton *b_file_up, *b_file_down, *b_track_up, *b_track_down;
  wxCheckBox *cb_no_chapters, *cb_no_attachments, *cb_no_tags;
  wxCheckBox *cb_default, *cb_aac_is_sbr;
  wxCheckListBox *clb_tracks;
  wxComboBox *cob_language, *cob_cues, *cob_sub_charset;
  wxComboBox *cob_aspect_ratio, *cob_fourcc;
  wxTextCtrl *tc_delay, *tc_track_name, *tc_stretch, *tc_tags, *tc_timecodes;
  wxComboBox *cob_compression;
  wxRadioButton *rb_aspect_ratio, *rb_display_dimensions;
  wxTextCtrl *tc_display_width, *tc_display_height;

  wxTimer value_copy_timer;

  int selected_file, selected_track;

public:
  tab_input(wxWindow *parent);

  void on_add_file(wxCommandEvent &evt);
  void on_remove_file(wxCommandEvent &evt);
  void on_move_file_up(wxCommandEvent &evt);
  void on_move_file_down(wxCommandEvent &evt);
  void on_file_selected(wxCommandEvent &evt);
  void on_move_track_up(wxCommandEvent &evt);
  void on_move_track_down(wxCommandEvent &evt);
  void on_track_selected(wxCommandEvent &evt);
  void on_track_enabled(wxCommandEvent &evt);
  void on_nochapters_clicked(wxCommandEvent &evt);
  void on_noattachments_clicked(wxCommandEvent &evt);
  void on_notags_clicked(wxCommandEvent &evt);
  void on_default_track_clicked(wxCommandEvent &evt);
  void on_aac_is_sbr_clicked(wxCommandEvent &evt);
  void on_language_selected(wxCommandEvent &evt);
  void on_cues_selected(wxCommandEvent &evt);
  void on_subcharset_selected(wxCommandEvent &evt);
  void on_browse_tags(wxCommandEvent &evt);
  void on_tags_changed(wxCommandEvent &evt);
  void on_delay_changed(wxCommandEvent &evt);
  void on_stretch_changed(wxCommandEvent &evt);
  void on_track_name_changed(wxCommandEvent &evt);
  void on_aspect_ratio_selected(wxCommandEvent &evt);
  void on_aspect_ratio_changed(wxCommandEvent &evt);
  void on_display_dimensions_selected(wxCommandEvent &evt);
  void on_display_width_changed(wxCommandEvent &evt);
  void on_display_height_changed(wxCommandEvent &evt);
  void on_fourcc_changed(wxCommandEvent &evt);
  void on_compression_selected(wxCommandEvent &evt);
  void on_value_copy_timer(wxTimerEvent &evt);
  void on_timecodes_changed(wxCommandEvent &evt);
  void on_browse_timecodes_clicked(wxCommandEvent &evt);

  void no_track_mode();
  void audio_track_mode(wxString ctype);
  void video_track_mode(wxString ctype);
  void subtitle_track_mode(wxString ctype);
  void enable_ar_controls(mmg_track_t *track);

  void save(wxConfigBase *cfg);
  void load(wxConfigBase *cfg);
  bool validate_settings();
};

#endif // __TAB_INPUT_H
