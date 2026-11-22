// SPDX-License-Identifier: GPL-3.0-or-later
#include "lina/ui/fault_text.hpp"

#include <QCoreApplication>

namespace lina::ui {

namespace {

QString text(const char *source) {
  return QCoreApplication::translate("lina::ui::faultText", source);
}

} // namespace

QString faultText(model::Fault fault) {
  using model::Fault;
  switch (fault) {
  case Fault::UnknownObject:
    return text("what it refers to does not exist");
  case Fault::IndexOutOfRange:
    return text("the place asked for is past the end of the list");
  case Fault::ValueOutOfRange:
    return text("a value is outside what LINA allows, or a name is empty");
  case Fault::WrongTrackKind:
    return text("this kind of track does not allow it");
  case Fault::WrongClipKind:
    return text("this kind of clip does not allow it");
  case Fault::SlotOccupied:
    return text("the slot already holds a clip");
  case Fault::ClipOverlap:
    return text("two clips on one track would overlap");
  case Fault::NoteOverlap:
    return text("two notes of the same pitch would overlap");
  case Fault::IdInvalid:
    return text("an identity is not valid here");
  case Fault::MediaInUse:
    return text("an audio clip still uses the file");
  case Fault::MediaMissing:
    return text("the audio file is not part of the project");
  case Fault::DeviceChainOrder:
    return text("an instrument can only be first, on a MIDI track");
  case Fault::SlotsMismatch:
    return text("a track's slots would not match the scenes");
  case Fault::SendsMismatch:
    return text("a track's sends would not match the return tracks");
  case Fault::OrderViolation:
    return text("a list would be out of order or hold something twice");
  case Fault::EnvelopeTarget:
    return text("the automation is not for that track, or exists already");
  case Fault::WrongInputKind:
    return text("this track cannot take that input");
  case Fault::IdsExhausted:
    return text("the project cannot number any more objects");
  case Fault::TextInvalid:
    return text("a text holds characters that cannot be stored");
  case Fault::NoTransaction:
  case Fault::TransactionOpen:
    return text("another edit is still going on");
  case Fault::NothingToUndo:
    return text("there is nothing to undo");
  case Fault::NothingToRedo:
    return text("there is nothing to redo");
  }
  return text("the edit breaks a rule of the project");
}

} // namespace lina::ui
