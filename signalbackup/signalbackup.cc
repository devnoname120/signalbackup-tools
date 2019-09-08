/*
    Copyright (C) 2019  Selwin van Dijk

    This file is part of signalbackup-tools.

    signalbackup-tools is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    signalbackup-tools is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with signalbackup-tools.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "signalbackup.ih"

SignalBackup::SignalBackup(std::string const &filename, std::string const &passphrase)
  :
  d_database(":memory:"),
  d_fd(new FileDecryptor(filename, passphrase, false)),
  d_passphrase(passphrase),
  d_ok(false)
{
  if (!d_fd->ok())
  {
    std::cout << "Failed to create filedecrypter" << std::endl;
    return;
  }

  uint64_t totalsize = d_fd->total();

  std::cout << "Reading backup file..." << std::endl;
  std::unique_ptr<BackupFrame> frame(nullptr);

  d_database.exec("BEGIN TRANSACTION");

  while ((frame = d_fd->getFrame())) // deal with bad mac??
  {

    if (d_fd->badMac())
    {
      std::cout << std::endl << "WARNING: Bad MAC in frame, trying to print frame info:" << std::endl;
      frame->printInfo();

      if (frame->frameType() == FRAMETYPE::ATTACHMENT)
      {
        std::unique_ptr<AttachmentFrame> a = std::make_unique<AttachmentFrame>(*reinterpret_cast<AttachmentFrame *>(frame.get()));
        //std::unique_ptr<AttachmentFrame> a(reinterpret_cast<AttachmentFrame *>(frame.release()));

        uint32_t rowid = a->rowId();
        uint64_t uniqueid = a->attachmentId();

        d_badattachments.emplace_back(std::make_pair(rowid, uniqueid));

        std::cout << "Frame is attachment, it belongs to entry in the 'part' table of the database:" << std::endl;
        //std::vector<std::vector<std::pair<std::string, std::any>>> results;
        SqliteDB::QueryResults results;
        std::string query = "SELECT * FROM part WHERE _id = " + bepaald::toString(rowid) + " AND unique_id = " + bepaald::toString(uniqueid);
        long long int mid = -1;
        d_database.exec(query, &results);
        for (uint i = 0; i < results.rows(); ++i)
        {
          for (uint j = 0; j < results.columns(); ++j)
          {
            std::cout << " - " << results.header(j) << " : ";
            if (results.valueHasType<nullptr_t>(i, j))
              std::cout << "(NULL)" << std::endl;
            else if (results.valueHasType<std::string>(i, j))
              std::cout << results.getValueAs<std::string>(i, j) << std::endl;
            else if (results.valueHasType<double>(i, j))
              std::cout << results.getValueAs<double>(i, j) << std::endl;
            else if (results.valueHasType<long long int>(i, j))
            {
              if (results.header(j) == "mid")
                mid = results.getValueAs<long long int>(i, j);
              std::cout << results.getValueAs<long long int>(i, j) << std::endl;
            }
            else if (results.valueHasType<std::pair<std::shared_ptr<unsigned char []>, size_t>>(i, j))
              std::cout << bepaald::bytesToHexString(results.getValueAs<std::pair<std::shared_ptr<unsigned char []>, size_t>>(i, j)) << std::endl;
           else
              std::cout << "(unhandled result type)" << std::endl;
          }
        }

        std::cout << std::endl << "Which belongs to entry in 'mms' table:" << std::endl;
        query = "SELECT * FROM mms WHERE _id = " + bepaald::toString(mid);
        d_database.exec(query, &results);

        for (uint i = 0; i < results.rows(); ++i)
          for (uint j = 0; j < results.columns(); ++j)
          {
            std::cout << " - " << results.header(j) << " : ";
            if (results.valueHasType<nullptr_t>(i, j))
              std::cout << "(NULL)" << std::endl;
            else if (results.valueHasType<std::string>(i, j))
              std::cout << results.getValueAs<std::string>(i, j) << std::endl;
            else if (results.valueHasType<double>(i, j))
              std::cout << results.getValueAs<double>(i, j) << std::endl;
            else if (results.valueHasType<long long int>(i, j))
            {
              if (results.header(j) == "date" ||
                  results.header(j) == "date_received")
              {
                long long int datum = results.getValueAs<long long int>(i, j);
                std::time_t epoch = datum / 1000;
                std::cout << std::put_time(std::localtime(&epoch), "%F %T %z") << " (" << results.getValueAs<long long int>(i, j) << ")" << std::endl;
              }
              else
                std::cout << results.getValueAs<long long int>(i, j) << std::endl;
            }
            else if (results.valueHasType<std::pair<std::shared_ptr<unsigned char []>, size_t>>(i, j))
              std::cout << bepaald::bytesToHexString(results.getValueAs<std::pair<std::shared_ptr<unsigned char []>, size_t>>(i, j)) << std::endl;
            else
              std::cout << "(unhandled result type)" << std::endl;
          }

        std::string afilename = "attachment_" + bepaald::toString(mid) + ".bin";
        std::cout << "Trying to dump decoded attachment to file '" << afilename << "'" << std::endl;

        std::ofstream bindump(afilename, std::ios::binary);
        bindump.write(reinterpret_cast<char *>(a->attachmentData()), a->attachmentSize());

      }
      else
      {
        std::cout << "Error: Bad MAC in frame other than AttachmentFrame. Not sure what to do..." << std::endl;
        frame->printInfo();
      }
    } // if (d_fd->badMac())

    std::cout << "\33[2K\rFRAME " << frame->frameNumber() << " ("
              << std::fixed << std::setprecision(1) << std::setw(5) << std::setfill('0')
              << (static_cast<float>(d_fd->curFilePos()) / totalsize) * 100 << "%)" << std::defaultfloat
              << "... " << std::flush;

    if (frame->frameType() == FRAMETYPE::HEADER)
    {
      d_headerframe.reset(reinterpret_cast<HeaderFrame *>(frame.release()));
      //d_headerframe->printInfo();
    }
    else if (frame->frameType() == FRAMETYPE::DATABASEVERSION)
    {
      d_databaseversionframe.reset(reinterpret_cast<DatabaseVersionFrame *>(frame.release()));
      //d_databaseversionframe->printInfo();
    }
    else if (frame->frameType() == FRAMETYPE::SQLSTATEMENT)
    {
      SqlStatementFrame *s = reinterpret_cast<SqlStatementFrame *>(frame.get());
      if (s->statement().find("CREATE TABLE sqlite_") == std::string::npos)
        d_database.exec(s->bindStatement(), s->parameters());
    }
    else if (frame->frameType() == FRAMETYPE::ATTACHMENT)
    {
      AttachmentFrame *a = reinterpret_cast<AttachmentFrame *>(frame.release());
      d_attachments.emplace(std::make_pair(a->rowId(), a->attachmentId()), a);
    }
    else if (frame->frameType() == FRAMETYPE::AVATAR)
    {
      AvatarFrame *a = reinterpret_cast<AvatarFrame *>(frame.release());
      d_avatars.emplace(std::move(std::string(a->name())), a);
    }
    else if (frame->frameType() == FRAMETYPE::SHAREDPREFERENCE)
      d_sharedpreferenceframes.emplace_back(reinterpret_cast<SharedPrefFrame *>(frame.release()));
    else if (frame->frameType() == FRAMETYPE::STICKER)
    {
      StickerFrame *s = reinterpret_cast<StickerFrame *>(frame.release());
      d_stickers.emplace(s->rowId(), s);
    }
    else if (frame->frameType() == FRAMETYPE::END)
      d_endframe.reset(reinterpret_cast<EndFrame *>(frame.release()));
  }

  d_database.exec("COMMIT");

  std::cout << "done!" << std::endl;

  d_ok = true;
}

SignalBackup::SignalBackup(std::string const &inputdir)
  :
  d_database(":memory:"),
  d_fe(),
  d_ok(false)
{

  std::cout << "Opening from dir!" << std::endl;

  SqliteDB database(inputdir + "/database.sqlite");
  if (!SqliteDB::copyDb(database, d_database))
    return;

  if (!setFrameFromFile(&d_headerframe, inputdir + "/Header.sbf"))
    return;

  //d_headerframe->printInfo();

  if (!setFrameFromFile(&d_databaseversionframe, inputdir + "/DatabaseVersion.sbf"))
    return;

  //d_databaseversionframe->printInfo();

  int idx = 0;
  while (true)
  {
    d_sharedpreferenceframes.resize(d_sharedpreferenceframes.size() + 1);
    if (!setFrameFromFile(&d_sharedpreferenceframes.back(), inputdir + "/SharedPreference_" + bepaald::toString(idx) + ".sbf", true))
    {
      d_sharedpreferenceframes.pop_back();
      break;
    }
    //d_sharedpreferenceframes.back()->printInfo();
    ++idx;
  }

  if (!setFrameFromFile(&d_endframe, inputdir + "/End.sbf"))
    return;

  //d_endframe->printInfo();

  // avatars
  std::error_code ec;
  std::filesystem::directory_iterator dirit(inputdir, ec);
  if (ec)
  {
    std::cout << "Error iterating directory `" << inputdir << "' : " << ec.message() << std::endl;
    return;
  }
  for (auto const &avatar : dirit)
  {
    if (avatar.path().filename().string().substr(0, STRLEN("Avatar_")) != "Avatar_" || avatar.path().extension() != ".sbf")
      continue;

    std::filesystem::path avatarframe = avatar.path();
    std::filesystem::path avatarbin = avatar.path();
    avatarbin.replace_extension(".bin");

    std::unique_ptr<AvatarFrame> temp;
    if (!setFrameFromFile(&temp, avatarframe.string()))
      return;
    temp->setAttachmentData(avatarbin.string());

    //temp->printInfo();

    std::string name = temp->name();

    d_avatars.emplace(name, temp.release());
  }

  //attachments
  dirit = std::filesystem::directory_iterator(inputdir, ec);
  if (ec)
  {
    std::cout << "Error iterating directory `" << inputdir << "' : " << ec.message() << std::endl;
    return;
  }
  for (auto const &att : dirit)
  {
    if (att.path().extension() != ".sbf" || att.path().filename().string().substr(0, STRLEN("Attachment_")) != "Attachment_")
      continue;

    std::filesystem::path attframe = att.path();
    std::filesystem::path attbin = att.path();
    attbin.replace_extension(".bin");

    std::unique_ptr<AttachmentFrame> temp;
    if (!setFrameFromFile(&temp, attframe.string()))
      return;
    temp->setAttachmentData(attbin.string());

    uint64_t rowid = temp->rowId();
    uint64_t attachmentid = temp->attachmentId();
    d_attachments.emplace(std::make_pair(rowid, attachmentid), temp.release());
  }

  //stickers
  dirit = std::filesystem::directory_iterator(inputdir, ec);
  if (ec)
  {
    std::cout << "Error iterating directory `" << inputdir << "' : " << ec.message() << std::endl;
    return;
  }
  for (auto const &sticker : dirit)
  {
    if (sticker.path().extension() != ".sbf" || sticker.path().filename().string().substr(0, STRLEN("Sticker_")) != "Sticker_")
      continue;

    std::filesystem::path stickerframe = sticker.path();
    std::filesystem::path stickerbin = sticker.path();
    stickerbin.replace_extension(".bin");

    std::unique_ptr<StickerFrame> temp;
    if (!setFrameFromFile(&temp, stickerframe.string()))
      return;
    temp->setAttachmentData(stickerbin.string());

    uint64_t rowid = temp->rowId();
    d_stickers.emplace(std::make_pair(rowid, temp.release()));
  }


  d_ok = true;
}

/*
void SignalBackup::exportXml(std::string const &filename) const
{
  if (checkFileExists(filename))
  {
    std::cout << "File " << filename << " exists. Refusing to overwrite" << std::endl;
    return;
  }

  SqliteDB::QueryResults results;
  d_database.exec("SELECT count(*) FROM sms", &results);
  long long int smscount = 0;
  if (results.valueHasType<long long int>(0, 0))
    smscount = results.getValueAs<long long int>(0, 0);
  d_database.exec("SELECT count(*) FROM mms", &results);
  long long int mmscount = 0;
  if (results.valueHasType<long long int>(0, 0))
    mmscount = results.getValueAs<long long int>(0, 0);

  std::ofstream outputfile(filename, std::ios_base::binary);
  outputfile << "<?xml version='1.0' encoding='UTF-8' standalone='yes' ?>";
  outputfile << "<?xml-stylesheet type=\"text/xsl\" href=\"sms.xsl\"?>";
  if (smscount)
  {
    outputfile << "<smses count=" << bepaald::toString(smscount) << ">";
    for (uint i = 0; i < smscount; ++i)
    {
      long long int protocol = 0; // optional, ubyte
      std::string address = "31647474974"; // required, string
      long long int date = 0; // req, ulong
      std::string readable_date = ""; // opt, string
      long long int type = 0; // req, ubyte
      std::string body = ""; // required, string
      std::string service_center = "null"; // opt, string // null for sent... ?? for received
      long long int read = 0; // req, ubyte
      long long int status = 0; // req, byte
      std::string contact_name = ""; // opt, string
      long long int locked = 0; // opt, ubyte;

      outputfile << "  <sms "
                 << "protocol=\"" << protocol << "\" "
                 << "address=\"" << address << "\" "
                 << "date=\"" << date << "\" "
                 << "type=\"" << type << "\" "
                 << "subject=\"" << "null" << "\" "
                 << "body=\"" << body << "\" "
                 << "toa=\"" << "null" << "\" "
                 << "sc_toa=\"" << "null" << "\" "
                 << "service_center=\"" << service_center << "\" "
                 << "read=\"" << read << "\" "
                 << "status=\"" << status << "\" "
                 << "locked=\"" << locked << "\" "
                 << "readable_date=\"" << readable_date << "\" "
                 << "contact_name=\"" << contact_name << "\" "
                 << "/>";
    }
    outputfile << "</smses>";
  }


  // protocol - Protocol used by the message, its mostly 0 in case of SMS messages.
  // address - The phone number of the sender/recipient.
  // date - The Java date representation (including millisecond) of the time when the message was sent/received. Check out www.epochconverter.com for information on how to do the conversion from other languages to Java.
  // type - 1 = Received, 2 = Sent, 3 = Draft, 4 = Outbox, 5 = Failed, 6 = Queued
  // subject - Subject of the message, its always null in case of SMS messages.
  // body - The content of the message.
  // toa - n/a, defaults to null.
  // sc_toa - n/a, defaults to null.
  // service_center - The service center for the received message, null in case of sent messages.
  // read - Read Message = 1, Unread Message = 0.
  // status - None = -1, Complete = 0, Pending = 32, Failed = 64.
  // readable_date - Optional field that has the date in a human readable format.
  // contact_name - Optional field that has the name of the contact.





  // mms
  // date - The Java date representation (including millisecond) of the time when the message was sent/received. Check out www.epochconverter.com for information on how to do the conversion from other languages to Java.
  // ct_t - The Content-Type of the message, usually "application/vnd.wap.multipart.related"
  // msg_box - The type of message, 1 = Received, 2 = Sent, 3 = Draft, 4 = Outbox
  // rr - The read-report of the message.
  // sub - The subject of the message, if present.
  // read_status - The read-status of the message.
  // address - The phone number of the sender/recipient.
  // m_id - The Message-ID of the message
  // read - Has the message been read
  // m_size - The size of the message.
  // m_type - The type of the message defined by MMS spec.
  // readable_date - Optional field that has the date in a human readable format.
  // contact_name - Optional field that has the name of the contact.
  // part
  // seq - The order of the part.
  // ct - The content type of the part.
  // name - The name of the part.
  // chset - The charset of the part.
  // cl - The content location of the part.
  // text - The text content of the part.
  // data - The base64 encoded binary content of the part.
  // addr
  // address - The phone number of the sender/recipient.
  // type - The type of address, 129 = BCC, 130 = CC, 151 = To, 137 = From
  // charset - Character set of this entry



  // Call Logs

  // number - The phone number of the call.
  // duration - The duration of the call in seconds.
  // date - The Java date representation (including millisecond) of the time when the message was sent/received. Check out www.epochconverter.com for information on how to do the conversion from other languages to Java.
  // type - 1 = Incoming, 2 = Outgoing, 3 = Missed, 4 = Voicemail, 5 = Rejected, 6 = Refused List.
  // presentation - caller id presentation info. 1 = Allowed, 2 = Restricted, 3 = Unknown, 4 = Payphone.
  // readable_date - Optional field that has the date in a human readable format.
  // contact_name - Optional field that has the name of the contact.



  //           <xs:complexType>
  //             <xs:sequence>
  //               <xs:element name="parts">
  //                 <xs:complexType>
  //                   <xs:sequence>
  //                     <xs:element maxOccurs="unbounded" name="part">
  //                       <xs:complexType>
  //                         <xs:attribute name="seq" type="xs:byte" use="required" />
  //                         <xs:attribute name="ct" type="xs:string" use="required" />
  //                         <xs:attribute name="name" type="xs:string" use="required" />
  //                         <xs:attribute name="chset" type="xs:string" use="required" />
  //                         <xs:attribute name="cd" type="xs:string" use="required" />
  //                         <xs:attribute name="fn" type="xs:string" use="required" />
  //                         <xs:attribute name="cid" type="xs:string" use="required" />
  //                         <xs:attribute name="cl" type="xs:string" use="required" />
  //                         <xs:attribute name="ctt_s" type="xs:string" use="required" />
  //                         <xs:attribute name="ctt_t" type="xs:string" use="required" />
  //                         <xs:attribute name="text" type="xs:string" use="required" />
  //                         <xs:attribute name="data" type="xs:string" use="optional" />
  //                       </xs:complexType>
  //                     </xs:element>
  //                   </xs:sequence>
  //                 </xs:complexType>
  //               </xs:element>
  //             </xs:sequence>
  //             <xs:attribute name="text_only" type="xs:unsignedByte" use="optional" />
  //             <xs:attribute name="sub" type="xs:string" use="optional" />
  //             <xs:attribute name="retr_st" type="xs:string" use="required" />
  //             <xs:attribute name="date" type="xs:unsignedLong" use="required" />
  //             <xs:attribute name="ct_cls" type="xs:string" use="required" />
  //             <xs:attribute name="sub_cs" type="xs:string" use="required" />
  //             <xs:attribute name="read" type="xs:unsignedByte" use="required" />
  //             <xs:attribute name="ct_l" type="xs:string" use="required" />
  //             <xs:attribute name="tr_id" type="xs:string" use="required" />
  //             <xs:attribute name="st" type="xs:string" use="required" />
  //             <xs:attribute name="msg_box" type="xs:unsignedByte" use="required" />
  //             <xs:attribute name="address" type="xs:long" use="required" />
  //             <xs:attribute name="m_cls" type="xs:string" use="required" />
  //             <xs:attribute name="d_tm" type="xs:string" use="required" />
  //             <xs:attribute name="read_status" type="xs:string" use="required" />
  //             <xs:attribute name="ct_t" type="xs:string" use="required" />
  //             <xs:attribute name="retr_txt_cs" type="xs:string" use="required" />
  //             <xs:attribute name="d_rpt" type="xs:unsignedByte" use="required" />
  //             <xs:attribute name="m_id" type="xs:string" use="required" />
  //             <xs:attribute name="date_sent" type="xs:unsignedByte" use="required" />
  //             <xs:attribute name="seen" type="xs:unsignedByte" use="required" />
  //             <xs:attribute name="m_type" type="xs:unsignedByte" use="required" />
  //             <xs:attribute name="v" type="xs:unsignedByte" use="required" />
  //             <xs:attribute name="exp" type="xs:string" use="required" />
  //             <xs:attribute name="pri" type="xs:unsignedByte" use="required" />
  //             <xs:attribute name="rr" type="xs:unsignedByte" use="required" />
  //             <xs:attribute name="resp_txt" type="xs:string" use="required" />
  //             <xs:attribute name="rpt_a" type="xs:string" use="required" />
  //             <xs:attribute name="locked" type="xs:unsignedByte" use="required" />
  //             <xs:attribute name="retr_txt" type="xs:string" use="required" />
  //             <xs:attribute name="resp_st" type="xs:string" use="required" />
  //             <xs:attribute name="m_size" type="xs:string" use="required" />
  //             <xs:attribute name="readable_date" type="xs:string" use="optional" />
  //             <xs:attribute name="contact_name" type="xs:string" use="optional" />
}
*/

void SignalBackup::listThreads() const
{
  SqliteDB::QueryResults results;
  d_database.exec("SELECT thread._id, thread.recipient_ids, thread.snippet, COALESCE(recipient_preferences.system_display_name, recipient_preferences.signal_profile_name, groups.title) FROM thread LEFT JOIN recipient_preferences ON thread.recipient_ids = recipient_preferences.recipient_ids LEFT JOIN groups ON thread.recipient_ids = groups.group_id ORDER BY thread._id ASC", &results);

  results.prettyPrint();

}

void SignalBackup::setMinimumId(std::string const &table, long long int offset) const
{
  if (offset == 0) // no changes requested
    return;

  if (offset < 0)
  {
    d_database.exec("UPDATE " + table + " SET _id = _id + (SELECT MAX(_id) from " + table + ") - (SELECT MIN(_id) from " + table + ") + ?", 1ll);
    d_database.exec("UPDATE " + table + " SET _id = _id - (SELECT MAX(_id) from " + table + ") + (SELECT MIN(_id) from " + table + ") + ?", (offset - 1));
  }
  else
  {
    d_database.exec("UPDATE " + table + " SET _id = _id + (SELECT MAX(_id) from " + table + ") - (SELECT MIN(_id) from " + table + ") + ?", offset);
    d_database.exec("UPDATE " + table + " SET _id = _id - (SELECT MAX(_id) from " + table + ") + (SELECT MIN(_id) from " + table + ")");
  }
}

void SignalBackup::cleanDatabaseByMessages()
{
  std::cout << "Deleting attachment entries from 'part' not belonging to remaining mms entries" << std::endl;
  d_database.exec("DELETE FROM part WHERE mid NOT IN (SELECT DISTINCT _id FROM mms)");

  std::cout << "Deleting other threads from 'thread'..." << std::endl;
  d_database.exec("DELETE FROM thread where _id NOT IN (SELECT DISTINCT thread_id FROM sms) AND _id NOT IN (SELECT DISTINCT thread_id FROM mms)");
  updateThreadsEntries();

  std::cout << "Deleting removed groups..." << std::endl;
  d_database.exec("DELETE FROM groups WHERE group_id NOT IN (SELECT DISTINCT recipient_ids FROM thread)");

  std::cout << "Deleting unreferenced recipient_preferences..." << std::endl;
  // this gets all recipient_ids/addresses ('+31612345678') from still existing groups and sms/mms
  d_database.exec("DELETE FROM recipient_preferences WHERE recipient_ids NOT IN (WITH RECURSIVE split(word, str) AS (SELECT '', members||',' FROM groups UNION ALL SELECT substr(str, 0, instr(str, ',')), substr(str, instr(str, ',')+1) FROM split WHERE str!='') SELECT DISTINCT split.word FROM split WHERE word!='' UNION SELECT DISTINCT address FROM sms UNION SELECT DISTINCT address FROM mms)");
  // remove avatars not belonging to exisiting recipients
  SqliteDB::QueryResults results;
  d_database.exec("SELECT recipient_ids FROM recipient_preferences", &results);
  for (std::map<std::string, std::unique_ptr<AvatarFrame>>::iterator avit = d_avatars.begin(); avit != d_avatars.end();)
    if (!results.contains(avit->first))
      avit = d_avatars.erase(avit);
    else
      ++avit;

  std::cout << "Delete others from 'identities'" << std::endl;
  d_database.exec("DELETE FROM identities WHERE address NOT IN (SELECT DISTINCT recipient_ids FROM recipient_preferences)");

  std::cout << "Deleting group receipts entries from deleted messages..." << std::endl;
  d_database.exec("DELETE FROM group_receipts WHERE mms_id NOT IN (SELECT DISTINCT _id FROM mms)");

  std::cout << "Deleting drafts from deleted threads..." << std::endl;
  d_database.exec("DELETE FROM drafts WHERE thread_id NOT IN (SELECT DISTINCT thread_id FROM sms) AND thread_id NOT IN (SELECT DISTINCT thread_id FROM mms)");
}

void SignalBackup::compactIds(std::string const &table)
{
  std::cout << "Compacting table: " << table << std::endl;

  SqliteDB::QueryResults results;
  // d_database.exec("SELECT _id FROM " + table, &results);
  // results.prettyPrint();

  d_database.exec("SELECT t1._id+1 FROM " + table + " t1 LEFT OUTER JOIN " + table + " t2 ON t2._id=t1._id+1 WHERE t2._id IS NULL AND t1._id > 0 ORDER BY t1._id LIMIT 1", &results);
  while (results.rows() > 0 && results.valueHasType<long long int>(0, 0))
  {
    long long int nid = results.getValueAs<long long int>(0, 0);

    d_database.exec("SELECT MIN(_id) FROM " + table + " WHERE _id > ?", nid, &results);
    if (results.rows() == 0 || !results.valueHasType<long long int>(0, 0))
      break;
    long long int valuetochange = results.getValueAs<long long int>(0, 0);

    //std::cout << "Changing _id : " << valuetochange << " -> " << nid << std::endl;

    d_database.exec("UPDATE " + table + " SET _id = ? WHERE _id = ?", {nid, valuetochange});


    if (table == "mms")
    {
      d_database.exec("UPDATE part SET mid = ? WHERE mid = ?", {nid, valuetochange}); // update part.mid to new mms._id's
      d_database.exec("UPDATE group_receipts SET mms_id = ? WHERE mms_id = ?", {nid, valuetochange}); // "
    }
    else if (table == "part")
    {
      std::map<std::pair<uint64_t, uint64_t>, std::unique_ptr<AttachmentFrame>> newattdb;
      for (auto &att : d_attachments)
        if (reinterpret_cast<AttachmentFrame *>(att.second.get())->rowId() == static_cast<uint64_t>(valuetochange))
          reinterpret_cast<AttachmentFrame *>(att.second.get())->setRowId(nid);
    }

    d_database.exec("SELECT t1._id+1 FROM " + table + " t1 LEFT OUTER JOIN " + table + " t2 ON t2._id=t1._id+1 WHERE t2._id IS NULL AND t1._id > 0 ORDER BY t1._id LIMIT 1", &results);
  }

  // d_database.exec("SELECT _id FROM " + table, &results);
  // results.prettyPrint();
}

void SignalBackup::makeIdsUnique(long long int minthread, long long int minsms, long long int minmms, long long int minpart, long long int minrecipient_preferences, long long int mingroups, long long int minidentities, long long int mingroup_receipts, long long int mindrafts)
{
  std::cout << "Adjusting indexes in tables..." << std::endl;

  setMinimumId("thread", minthread);
  d_database.exec("UPDATE sms SET thread_id = thread_id + ?", minthread);    // update sms.thread_id to new thread._id's
  d_database.exec("UPDATE mms SET thread_id = thread_id + ?", minthread);    // ""
  d_database.exec("UPDATE drafts SET thread_id = thread_id + ?", minthread); // ""

  setMinimumId("sms",  minsms);
  compactIds("sms");

  // UPDATE t SET id = (SELECT t1.id+1 FROM t t1 LEFT OUTER JOIN t t2 ON t2.id=t1.id+1 WHERE t2.id IS NULL AND t1.id > 0 ORDER BY t1.id LIMIT 1) WHERE id = (SELECT MIN(id) FROM t WHERE id > (SELECT t1.id+1 FROM t t1 LEFT OUTER JOIN t t2 ON t2.id=t1.id+1 WHERE t2.id IS NULL AND t1.id > 0 ORDER BY t1.id LIMIT 1));

  setMinimumId("mms",  minmms);
  d_database.exec("UPDATE part SET mid = mid + ?", minmms); // update part.mid to new mms._id's
  d_database.exec("UPDATE group_receipts SET mms_id = mms_id + ?", minmms); // "
  compactIds("mms");

  setMinimumId("part", minpart);
  // update rowid's in attachments
  std::map<std::pair<uint64_t, uint64_t>, std::unique_ptr<AttachmentFrame>> newattdb;
  for (auto &att : d_attachments)
  {
    AttachmentFrame *a = reinterpret_cast<AttachmentFrame *>(att.second.release());
    a->setRowId(a->rowId() + minpart);
    newattdb.emplace(std::make_pair(a->rowId(), a->attachmentId()), a);
  }
  d_attachments = std::move(newattdb);
  compactIds("part");

  setMinimumId("recipient_preferences", minrecipient_preferences);
  compactIds("recipient_preferences");

  setMinimumId("groups", mingroups);
  compactIds("groups");

  setMinimumId("identities", minidentities);
  compactIds("identities");

  setMinimumId("group_receipts", mingroup_receipts);
  compactIds("group_receipts");

  setMinimumId("drafts", mindrafts);
  compactIds("drafts");
}

long long int SignalBackup::getMinUsedId(std::string const &table)
{
  SqliteDB::QueryResults results;
  d_database.exec("SELECT MIN(_id) FROM " + table, &results);
  if (results.rows() != 1 ||
      results.columns() != 1 ||
      !results.valueHasType<long long int>(0, 0))
  {
    return 0;
  }
  return results.getValueAs<long long int>(0, 0);
}

long long int SignalBackup::getMaxUsedId(std::string const &table)
{
  SqliteDB::QueryResults results;
  d_database.exec("SELECT MAX(_id) FROM " + table, &results);
  if (results.rows() != 1 ||
      results.columns() != 1 ||
      !results.valueHasType<long long int>(0, 0))
  {
    return 0;
  }
  return results.getValueAs<long long int>(0, 0);
}

long long int SignalBackup::dateToMSecsSinceEpoch(std::string const &date) const
{
  long long int ret = -1;
  std::tm t = {};
  std::istringstream ss(date);
  if (ss >> std::get_time(&t, "%Y-%m-%d %H:%M:%S"))
    ret = std::mktime(&t) * 1000;
  return ret;
}

void SignalBackup::showQuery(std::string const &query) const
{
  SqliteDB::QueryResults results;
  d_database.exec(query, &results);
  results.prettyPrint();
}

void SignalBackup::cropToDates(std::vector<std::pair<std::string, std::string>> const &dateranges)
{

  std::string smsq;
  std::string mmsq;
  std::vector<std::any> params;
  for (uint i = 0; i < dateranges.size(); ++i)
  {
    long long int startrange = dateToMSecsSinceEpoch(dateranges[i].first);
    long long int endrange   = dateToMSecsSinceEpoch(dateranges[i].second);
    if (startrange == -1 || endrange == -1 || endrange < startrange)
    {
      std::cout << "Error: Skipping range: '" << dateranges[i].first << " - " << dateranges[i].second << "'. Failed to parse or invalid range." << std::endl;
      continue;
    }
    endrange += 999; // to get everything in the second specified...

    if (i == 0)
    {
      smsq = "DELETE FROM sms WHERE ";
      mmsq = "DELETE FROM mms WHERE ";
    }
    else
    {
      smsq += "AND ";
      mmsq += "AND ";
    }
    smsq += "date NOT BETWEEN ? AND ?";
    mmsq += "date_received NOT BETWEEN ? AND ?";
    if (i < dateranges.size() - 1)
    {
      smsq += " ";
      mmsq += " ";
    }
    params.emplace_back(startrange);
    params.emplace_back(endrange);
  }
  if (smsq.empty() || mmsq.empty())
  {
    std::cout << "Error: Failed to get any date ranges.";
    return;
  }

  d_database.exec(smsq, params);
  d_database.exec(mmsq, params);
  cleanDatabaseByMessages();
}

void SignalBackup::addSMSMessage(std::string const &body, std::string const &address, std::string const &timestamp, long long int thread, bool incoming)
{
  addSMSMessage(body, address, dateToMSecsSinceEpoch(timestamp), thread, incoming);
}

void SignalBackup::addSMSMessage(std::string const &body, std::string const &address, long long int timestamp, long long int thread, bool incoming)
{
  //get address automatically -> msg partner for normal thread, sender for incoming group, groupid (__textsecure__!xxxxx) for outgoing
  // maybe do something with 'notified'? it is almost always 0, but a few times it is 1 on incoming msgs in my db


  // INSERT INTO sms(_id, thread_id, address, address_device_id, person, date, date_sent, protocol, read, status, type, reply_path_present, delivery_receipt_count, subject, body, mismatched_identities, service_center, subscription_id, expires_in, expire_started, notified, read_receipt_count, unidentified);

  if (incoming)
  {
    d_database.exec("INSERT INTO sms(thread_id, body, date, date_sent, address, type, protocol, read, reply_path_present, service_center) VALUES(?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                    {thread, body, timestamp, timestamp, address, 10485780ll, 31337ll, 1ll, 1ll, std::string("GCM")});
  }
  else
  {
    d_database.exec("INSERT INTO sms(thread_id, body, date, date_sent, address, type, read, delivery_receipt_count) VALUES(?, ?, ?, ?, ?, ?, ?, ?)",
                    {thread, body, timestamp, timestamp, address, 10485783ll, 1ll, 1ll});
  }

  // update message count
  updateThreadsEntries(thread);

  /*
    protocol on incoming is 31337? on outgoing always null

    what i know about types quickly...
    type
    10485784 = outgoing, send failed
    2097684 = incoming, safety number changed?
    10551319 = outgoing, updated group
    3 = call
    2 = call
    10485783 = outgoing normal (secure) message, properly received and such
    10458780 = incoming normal (secure) message, properly received and such?
    10747924 = incoming, disabled disappearing msgs
  */
}

/*
void SignalBackup::addMMSMessage()
{

MMS TABLE:
CREATE TABLE mms (
_id INTEGER PRIMARY KEY,
** thread_id INTEGER,
** date INTEGER,
** date_received INTEGER,
** msg_box INTEGER,
read INTEGER DEFAULT 0,
m_id TEXT,
sub TEXT,
sub_cs INTEGER,
** body TEXT,
part_count INTEGER,
ct_t TEXT,
ct_l TEXT,
** address TEXT,  -> for goup messages, this is '__textsecure_group__!xxxxx...' for outgoing, +316xxxxxxxx for incoming
address_device_id INTEGER,
exp INTEGER,
m_cls TEXT,
m_type INTEGER,  --> in my database either 132 OR 128, always 128 for outgoing, 132 for incoming
v INTEGER,
m_size INTEGER,
pri INTEGER,
rr INTEGER,
rpt_a INTEGER,
resp_st INTEGER,
st INTEGER,       --> null, or '1' in my database, always null for outgoing, 1 for incoming
tr_id TEXT,
retr_st INTEGER,
retr_txt TEXT,
retr_txt_cs INTEGER,
read_status INTEGER,
ct_cls INTEGER,
resp_txt TEXT,
d_tm INTEGER,
delivery_receipt_count INTEGER DEFAULT 0, --> set auto to number of recipients
mismatched_identities TEXT DEFAULT NULL,
network_failures TEXT DEFAULT NULL,
d_rpt INTEGER,
subscription_id INTEGER DEFAULT -1,
expires_in INTEGER DEFAULT 0,
expire_started INTEGER DEFAULT 0,
notified INTEGER DEFAULT 0,              --> both 0 and 1 present, most often '0' (32553 vs 199), only 1 on incoming types
read_receipt_count INTEGER DEFAULT 0,    --> always zero for me... but...
quote_id INTEGER DEFAULT 0,
quote_author TEXT,
quote_body TEXT,
quote_attachment INTEGER DEFAULT -1,     --> always -1 in my db??
shared_contacts TEXT,
quote_missing INTEGER DEFAULT 0,
unidentified INTEGER DEFAULT 0,           --> both 0 and 1 in my db
previews TEXT)                           -->



CREATE TABLE part (
_id INTEGER PRIMARY KEY,
mid INTEGER,
seq INTEGER DEFAULT 0,
ct TEXT,
name TEXT,
chset INTEGER,
cd TEXT,
fn TEXT,
cid TEXT,
cl TEXT,c
tt_s INTEGER,
ctt_t TEXT,
encrypted INTEGER,
pending_push INTEGER,
_data TEXT,
data_size INTEGER,
file_name TEXT,
thumbnail TEXT,
aspect_ratio REAL,
unique_id INTEGER NOT NULL,
digest BLOB,
fast_preflight_id TEXT,
voice_note INTEGER DEFAULT 0,
data_random BLOB,
thumbnail_random BLOB,
width INTEGER DEFAULT 0,
height INTEGER DEFAULT 0,
quote INTEGER DEFAULT 0,
caption TEXT DEFAULT NULL)
}
*/

/*
replaceAttachment()
*/

bool SignalBackup::dropBadFrames()
{
  if (d_badattachments.empty())
    return true;

  std::cout << "Removing " << d_badattachments.size() << " bad frames from database..." << std::endl;
  for (auto it = d_badattachments.begin(); it != d_badattachments.end(); )
  {
    uint32_t rowid = it->first;
    uint64_t uniqueid = it->second;

    SqliteDB::QueryResults results;
    std::string query = "SELECT mid FROM part WHERE _id = " + bepaald::toString(rowid) + " AND unique_id = " + bepaald::toString(uniqueid);
    long long int mid = -1;
    d_database.exec(query, &results);
    for (uint i = 0; i < results.rows(); ++i)
      for (uint j = 0; j < results.columns(); ++j)
        if (results.valueHasType<long long int>(i, j))
          if (results.header(j) == "mid")
          {
            mid = results.getValueAs<long long int>(i, j);
            break;
          }

    if (mid == -1)
    {
      std::cout << "Failed to remove frame :( Could not find matching 'part' entry" << std::endl;
      return false;
    }

    d_database.exec("DELETE FROM part WHERE mid = " + bepaald::toString(mid));
    d_badattachments.erase(it);
  }

  return true;
}
