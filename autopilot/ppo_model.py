"""The PPO policy's network (train_ppo.py trains it, policy_ppo.py drives with it)."""
import torch
import torch.nn as nn


class Policy(nn.Module):
    def __init__(self, obs_dim, img=None):
        super().__init__()
        self.img_size = img
        self.vec = nn.Sequential(nn.Linear(obs_dim, 256), nn.Tanh(), nn.Linear(256, 256), nn.Tanh())
        width = 256
        if img:
            self.cnn = nn.Sequential(nn.Conv2d(1, 16, 5, 2), nn.ReLU(), nn.Conv2d(16, 32, 5, 2), nn.ReLU(),
                                     nn.Conv2d(32, 32, 3, 2), nn.ReLU(), nn.Flatten())
            with torch.no_grad():
                n = self.cnn(torch.zeros(1, 1, img[1], img[0])).shape[1]
            self.img = nn.Sequential(nn.Linear(n, 128), nn.ReLU())
            width += 128
        self.mu = nn.Linear(width, 2)
        self.v = nn.Linear(width, 1)
        self.logstd = nn.Parameter(torch.full((2,), -1.0))
        # Start where the base driver drives: on its line, its throttle.
        with torch.no_grad():
            self.mu.weight.mul_(0.01)
            self.mu.bias.copy_(torch.tensor([0.0, 1.0]))

    def forward(self, vec, img=None):
        h = self.vec(vec)
        if self.img_size:
            h = torch.cat([h, self.img(self.cnn(img))], dim=1)
        return torch.tanh(self.mu(h)), self.v(h).squeeze(-1)
