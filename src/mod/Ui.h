#pragma once
namespace bmm::ui {
void draw();          // called once per frame between NewFrame/Render
bool wantsInput();    // true while the full menu is open (swallow keyboard from the game)
}
