// xử lý các yêu cầu POST
// dạng json {
//     "command": "",
//     "content1": "",
//     "content2": "",
//     "content3": "",
//     "content4": ""
// }

function doPost(e) {
  try {
    if (e.postData && e.postData.contents) {
      const data = JSON.parse(e.postData.contents); 
        if (data.command === "Request") {
          const mailBody = `<p style="font-size:16px;">Có yêu cầu tài khoản mới từ <b>${data.content1}</b></p>`;

          MailApp.sendEmail({
            to: "phat.nguyen2706@hcmut.edu.vn",
            subject: "ACS: có yêu cầu tài khoản mới",
            htmlBody: mailBody
          });
          return ContentService.createTextOutput("Forwarded Successfully.");
        } else if (data.command === "Lost") {
            const mailBody = `<p style="font-size:16px;">Người dùng <b>${data.content1}</b> đã báo mất thẻ</p>`;

            MailApp.sendEmail({
              to: "phat.nguyen2706@hcmut.edu.vn",
              subject: "ACS: có thông báo mất thẻ",
              htmlBody: mailBody
            });
            return ContentService.createTextOutput("Forwarded Successfully");
          } else if (data.command === "Upload") {
              const mailBody = `<p style="font-size:16px;">Người dùng <b>${data.content1}</b> vừa mới cập nhật thông tin</p>`;

              MailApp.sendEmail({
                to: "phat.nguyen2706@hcmut.edu.vn",
                subject: "ACS: có người dùng cập nhật thông tin",
                htmlBody: mailBody
              });
              return ContentService.createTextOutput("Forwarded Successfully");
            } else if (data.command === "Generate") {
                const mailBody = `<p style="font-size:16px;">Email: <b>${data.content1}</b></p>` +
                                  `<p style="font-size:16px;">Tài khoản: <b>${data.content2}</b></p>` +
                                  `<p style="font-size:16px;">Mật khẩu: <b>${data.content3}</b></p>`;

                MailApp.sendEmail({
                  to: "phat.nguyen2706@hcmut.edu.vn",
                  subject: "ACS: tài khoản mới",
                  htmlBody: mailBody
                });
                return ContentService.createTextOutput("Forwarded Successfully");
              } else if (data.command === "Alert") {

                  let imageHtml = "";
                  if (data.content4 === "không có") {
                    imageHtml = `<p style="font-size:16px;">Ảnh chụp được: <b>không có</b></p>`;
                  } else {
                    imageHtml = `<p style="font-size:16px;">Ảnh chụp được:<br><img src="${data.content4}" alt="Hình ảnh ghi nhận" style="max-width:100%; height:auto;"></p>`;
                  }
                  const mailBody =
                    `<p style="font-size:16px;">Thời gian: <b>${data.content1} - ${data.content2}</b></p>` +
                    `<p style="font-size:16px;">Loại xác thực: <b>${data.content3}</b></p>` +
                    imageHtml;
                  MailApp.sendEmail({
                    to: "phat.nguyen2706@hcmut.edu.vn",
                    subject: "ACS: phát hiện truy cập bất thường",
                    htmlBody: mailBody
                  });
                  return ContentService.createTextOutput("Forwarded Successfully");
                }
    } 
  } catch (error) {
    return ContentService.createTextOutput("Error: " + error.message);
  }
}


